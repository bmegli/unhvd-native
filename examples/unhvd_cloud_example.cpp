/*
 * UNHVD Network Hardware Video Decoder example
 *
 * Copyright 2020 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *
 * Note that UNHVD was written for integration with Unity
 * - it assumes that engine checks just before rendering if new frame has arrived
 * - this example simulates such behaviour be sleeping frame time
 * - for those reason UNHVD may not fit your workflow
 */

#include "../unhvd.h"

#include <iostream>
#include <unistd.h> //usleep, note that this is not portable

using namespace std;

void main_loop(unhvd *network_decoder);
int process_user_input(int argc, char **argv, unhvd_hw_config *hw_config, unhvd_net_config *net_config);

//network configuration
const char *IP=NULL; //listen on or NULL (listen on any)
const uint16_t PORT=9768; //to be input through CLI
const int TIMEOUT_MS=500; //timeout, accept new streaming sequence by receiver

//decoder configuration
const char *HARDWARE=NULL; //input through CLI, e.g. "vaapi"
const char *CODEC="hevc";  //we assume hevc for both depth and texture
const char *DEVICE=NULL; //optionally input through CLI, e.g. "/dev/dri/renderD128"
const char *PIXEL_FORMAT_DEPTH="p010le"; //we encode depth map with p010le pixel format
const char *PIXEL_FORMAT_TEXTURE="rgb0"; //we want data in rgb0, we expect it in unprojection
const int WIDTH=0; //0 to not specify, needed by some codecs
const int HEIGHT=0; //0 to not specify, needed by some codecs
const int PROFILE_DEPTH=2; //hardcoded for now, we need 10 bit HEVC Main 10 for depth encoding
const int PROFILE_TEXTURE=1; //hardcoded for now, we use HEVC Main for texture encoding

//for list of profiles see:
//https://ffmpeg.org/doxygen/3.4/avcodec_8h.html#ab424d258655424e4b1690e2ab6fcfc66

//depth unprojection configuration
const float PPX=421.353;
const float PPY=240.93;
const float FX=426.768;
const float FY=426.768;
const float DEPTH_UNIT=0.0001;

//we simpulate application rendering at framerate
const int FRAMERATE = 30;

int main(int argc, char **argv)
{
	unhvd_net_config net_config = {IP, PORT, TIMEOUT_MS};

	unhvd_hw_config hw_config[2] = {
		{HARDWARE, CODEC, DEVICE, PIXEL_FORMAT_DEPTH, WIDTH, HEIGHT, PROFILE_DEPTH},
		{HARDWARE, CODEC, DEVICE, PIXEL_FORMAT_TEXTURE, WIDTH, HEIGHT, PROFILE_TEXTURE}
	                           };

	unhvd_depth_config depth_config = {PPX, PPY, FX, FY, DEPTH_UNIT};

	if(process_user_input(argc, argv, hw_config, &net_config) != 0)
		return 1;

	unhvd *network_decoder = unhvd_init(&net_config, hw_config, 2, &depth_config);

	if(!network_decoder)
	{
		cerr << "failed to initalize unhvd" << endl;
		return 2;
	}

	main_loop(network_decoder);

	unhvd_close(network_decoder);
	return 0;
}

void main_loop(unhvd *network_decoder)
{
	const int SLEEP_US = 1000000/FRAMERATE;

	//this is where we will get the decoded data
	unhvd_frame frame;
	unhvd_point_cloud cloud;

	bool keep_working=true;

	while(keep_working)
	{
		if( unhvd_get_point_cloud_begin(network_decoder, &cloud) == UNHVD_OK )
		{
			//do something with:
			// - cloud.data
			// - cloud.colors
			// - cloud.size
			// - cloud.used
			cout << "Decoded cloud with " << cloud.used  << " points" << endl;
		}

		if( unhvd_get_point_cloud_end(network_decoder) != UNHVD_OK )
			break; //error occured

		//this should spin once per frame rendering
		//so wait until we are after rendering
		usleep(SLEEP_US);
	}
}

int process_user_input(int argc, char **argv, unhvd_hw_config *hw_config, unhvd_net_config *net_config)
{
	if(argc < 3)
	{
		fprintf(stderr, "Usage: %s <port> <hardware> [device] [width] [height] \n\n", argv[0]);
		fprintf(stderr, "examples: \n");
		fprintf(stderr, "%s 9768 vaapi /dev/dri/renderD128 640 360\n", argv[0]);
		fprintf(stderr, "%s 9768 vaapi /dev/dri/renderD128 848 480\n", argv[0]);

		return 1;
	}

	net_config->port = atoi(argv[1]);

	hw_config[0].hardware = argv[2];
	hw_config[1].hardware = argv[2];

	hw_config[0].device = argv[3]; //NULL or device, both are ok
	hw_config[1].device = argv[3]; //NULL or device, both are ok

	if(argc > 4) hw_config[0].width = atoi(argv[4]);
	if(argc > 5) hw_config[0].height = atoi(argv[5]);

	if(argc > 4) hw_config[1].width = atoi(argv[4]);
	if(argc > 5) hw_config[1].height = atoi(argv[5]);

	return 0;
}
