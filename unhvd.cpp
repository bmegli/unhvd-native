/*
 * UNHVD Network Hardware Video Decoder plugin C++ library implementation
 *
 * Copyright 2019-2020 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "unhvd.h"

// Network Hardware Video Decoder library
#include "nhvd.h"
// Hardware Depth Unprojector library
#include "hdu.h"

#include <thread>
#include <mutex>
#include <fstream>
#include <iostream>
#include <string.h> //memset

using namespace std;

static void unhvd_network_decoder_thread(unhvd *n);
static int unhvd_unproject_depth_frame(unhvd *n, const AVFrame *depth_frame, const AVFrame *texture_frame);
static unhvd *unhvd_close_and_return_null(unhvd *n, const char *msg);
static int UNHVD_ERROR_MSG(const char *msg);

struct unhvd
{
	nhvd *network_decoder;
	int decoders;

	AVFrame *frame[UNHVD_MAX_DECODERS];
	std::mutex mutex; //guards frame and point_cloud

	hdu *hardware_unprojector;
	hdu_point_cloud point_cloud;

	thread network_thread;
	bool keep_working;

	unhvd():
			network_decoder(NULL),
			decoders(0),
			frame(), //zero out
			hardware_unprojector(NULL),
			point_cloud(),
			keep_working(true)
	{}
};

struct unhvd *unhvd_init(
	const unhvd_net_config *net_config,
	const unhvd_hw_config *hw_config, int hw_size,
	const unhvd_depth_config *depth_config)
{
	nhvd_net_config nhvd_net = {net_config->ip, net_config->port, net_config->timeout_ms};
	nhvd_hw_config nhvd_hw[UNHVD_MAX_DECODERS] = {0};

	if(hw_size > UNHVD_MAX_DECODERS)
		return unhvd_close_and_return_null(NULL, "the maximum number of decoders (compile time) exceeded");

	unhvd *u=new unhvd();

	if(u == NULL)
		return unhvd_close_and_return_null(NULL, "not enough memory for UNHVD");

	for(int i=0;i<hw_size;++i)
	{
		nhvd_hw_config hw = {hw_config[i].hardware, hw_config[i].codec, hw_config[i].device,
		hw_config[i].pixel_format, hw_config[i].width, hw_config[i].height, hw_config[i].profile};

		nhvd_hw[i] = hw;
	}

	if( (u->network_decoder = nhvd_init(&nhvd_net, nhvd_hw, hw_size)) == NULL)
		return unhvd_close_and_return_null(u, "failed to initialize NHVD");

	u->decoders = hw_size;

	for(int i=0;i<hw_size;++i)
	{
		if( (u->frame[i] = av_frame_alloc() ) == NULL)
			return unhvd_close_and_return_null(u, "not enough memory for video frame");

		u->frame[i]->data[0] = NULL;
	}

	if(depth_config)
	{
		const unhvd_depth_config *dc = depth_config;
		if( (u->hardware_unprojector = hdu_init(dc->ppx, dc->ppy, dc->fx, dc->fy, dc->depth_unit)) == NULL )
			return unhvd_close_and_return_null(u, "failed to initialize hardware unprojector");
	}

	u->network_thread = thread(unhvd_network_decoder_thread, u);

	return u;
}

static void unhvd_network_decoder_thread(unhvd *u)
{
	AVFrame *frames[UNHVD_MAX_DECODERS];
	int status;


	while( u->keep_working &&
	     ((status = nhvd_receive(u->network_decoder, frames) ) != NHVD_ERROR) )
	{
		if(status == NHVD_TIMEOUT)
			continue; //keep working

		//the next calls to nhvd_receive will unref the current
		//frames so we have to either consume set of frames or ref it
		std::lock_guard<std::mutex> frame_guard(u->mutex);

		for(int i=0;i<u->decoders;++i)
			if(frames[i])
			{
				av_frame_unref(u->frame[i]);
				av_frame_ref(u->frame[i], frames[i]);
			}

		if(u->hardware_unprojector && frames[0])
			if(unhvd_unproject_depth_frame(u, u->frame[0], u->frame[1]) != UNHVD_OK)
				break;
	}

	if(u->keep_working)
		cerr << "unhvd: network decoder fatal error" << endl;

	cerr << "unhvd: network decoder thread finished" << endl;
}


static int unhvd_unproject_depth_frame(unhvd *u, const AVFrame *depth_frame, const AVFrame *texture_frame)
{
	if(depth_frame->linesize[0] / depth_frame->width != 2 ||
		(depth_frame->format != AV_PIX_FMT_P010LE && depth_frame->format != AV_PIX_FMT_P016LE))
		return UNHVD_ERROR_MSG("unhvd_unproject_depth_frame expects uint16 p010le/p016le data");

	if(texture_frame && texture_frame->data[0] &&
		texture_frame->format != AV_PIX_FMT_RGB0 && texture_frame->format != AV_PIX_FMT_RGBA)
		return UNHVD_ERROR_MSG("unhvd_unproject_depth_frame expects RGB0/RGBA texture data");

	int size = depth_frame->width * depth_frame->height;
	if(size != u->point_cloud.size)
	{
		delete [] u->point_cloud.data;
		delete [] u->point_cloud.colors;
		u->point_cloud.data = new float3[size];
		u->point_cloud.colors = new color32[size];
		u->point_cloud.size = size;
		u->point_cloud.used = 0;
	}

	uint16_t *depth_data = (uint16_t*)depth_frame->data[0];
	//texture data is optional
	uint32_t *texture_data = texture_frame ? (uint32_t*)texture_frame->data[0] : NULL;
	int texture_linesize = texture_frame ? texture_frame->linesize[0] : 0;

	hdu_depth depth = {depth_data, texture_data, depth_frame->width, depth_frame->height,
		depth_frame->linesize[0], texture_linesize};
	//this could be moved to separate thread
	hdu_unproject(u->hardware_unprojector, &depth, &u->point_cloud);
	//zero out unused point cloud entries
	memset(u->point_cloud.data + u->point_cloud.used, 0, (u->point_cloud.size-u->point_cloud.used)*sizeof(u->point_cloud.data[0]));
	memset(u->point_cloud.colors + u->point_cloud.used, 0, (u->point_cloud.size-u->point_cloud.used)*sizeof(u->point_cloud.colors[0]));

	return UNHVD_OK;
}

//NULL if there is no fresh data, non NULL otherwise
int unhvd_get_begin(unhvd *u, unhvd_frame *frame, unhvd_point_cloud *pc)
{
	if(u == NULL)
		return UNHVD_ERROR;

	u->mutex.lock();

	bool new_data = false;

	for(int i=0;i<u->decoders;++i)
		if(u->frame[i]->data[0] != NULL)
			new_data = true;

	//for user convinience, return ERROR if there is no new data
	if(!new_data)
		return UNHVD_ERROR;

	if(frame)
		for(int i=0;i<u->decoders;++i)
		{
			frame[i].width = u->frame[i]->width;
			frame[i].height = u->frame[i]->height;
			frame[i].format = u->frame[i]->format;

			//copy just a few ints and pointers, not the actual data
			memcpy(frame[i].linesize, u->frame[i]->linesize, sizeof(frame[i].linesize));
			memcpy(frame[i].data, u->frame[i]->data, sizeof(frame[i].data));
		}

	//future -consider only updating if updated frame[0] or other way
	if(pc && u->hardware_unprojector)
	{
		//copy just two pointers and ints
		pc->data = u->point_cloud.data;
		pc->colors = u->point_cloud.colors;
		pc->size = u->point_cloud.size;
		pc->used = u->point_cloud.used;
	}

	return UNHVD_OK;
}

//returns UNHVD_OK on success, UNHVD_ERROR on fatal error
int unhvd_get_end(struct unhvd *u)
{
	if(u == NULL)
		return UNHVD_ERROR;

	for(int i=0;i<u->decoders;++i)
		av_frame_unref(u->frame[i]);

	u->mutex.unlock();

	return UNHVD_OK;
}

int unhvd_get_frame_begin(unhvd *u, unhvd_frame *frame)
{
	return unhvd_get_begin(u, frame, NULL);
}

int unhvd_get_frame_end(struct unhvd *u)
{
	return unhvd_get_end(u);
}

int unhvd_get_point_cloud_begin(unhvd *u, unhvd_point_cloud *pc)
{
	return unhvd_get_begin(u, NULL, pc);
}

int unhvd_get_point_cloud_end(unhvd *u)
{
	return unhvd_get_end(u);
}

static unhvd *unhvd_close_and_return_null(unhvd *u, const char *msg)
{
	if(msg)
		cerr << "unhvd: " << msg << endl;

	unhvd_close(u);

	return NULL;
}

void unhvd_close(unhvd *u)
{	//free mutex?
	if(u == NULL)
		return;

	u->keep_working=false;
	if(u->network_thread.joinable())
		u->network_thread.join();

	nhvd_close(u->network_decoder);

	for(int i=0;i<u->decoders;++i)
		av_frame_free(&u->frame[i]);

	hdu_close(u->hardware_unprojector);
	delete [] u->point_cloud.data;
	delete [] u->point_cloud.colors;

	delete u;
}

static int UNHVD_ERROR_MSG(const char *msg)
{
	cerr << "unhvd: " << msg << endl;
	return UNHVD_ERROR;
}