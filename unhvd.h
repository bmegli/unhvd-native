/*
 * UNHVD Unity Network Hardware Video Decoder plugin C++ library header
 *
 * Copyright 2019-2020 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef UNHVD_H
#define UNHVD_H

#include <stdint.h>

/**
 ******************************************************************************
 *
 *  \mainpage UNHVD documentation
 *  \see https://github.com/bmegli/unity-network-hardware-video-decoder
 *
 *  \copyright  Copyright (C) 2019-2020 Bartosz Meglicki
 *  \file       unhvd.h
 *  \brief      Library public interface header
 *
 ******************************************************************************
 */

// API compatible with C99 on various platorms
// Compatible with Unity Native Plugins
#if defined(__CYGWIN32__)
    #define UNHVD_API __stdcall
    #define UNHVD_EXPORT __declspec(dllexport)
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(WINAPI_FAMILY)
    #define UNHVD_API __stdcall
    #define UNHVD_EXPORT __declspec(dllexport)
#elif defined(__MACH__) || defined(__ANDROID__) || defined(__linux__)
    #define UNHVD_API
    #define UNHVD_EXPORT
#else
    #define UNHVD_API
    #define UNHVD_EXPORT
#endif

extern "C"{

/** \addtogroup interface Public interface
 *  @{
 */

/**
 * @struct unhvd
 * @brief Internal library data passed around by the user.
 * @see unhvd_init, unhvd_close
 */
struct unhvd;

/**
 * @struct unhvd_net_config
 * @brief Network configuration.
 *
 * For more details see:
 * <a href="https://github.com/bmegli/minimal-latency-streaming-protocol">MLSP</a>
 *
 * @see unhvd_init
 */
struct unhvd_net_config
{
	const char *ip; //!< IP (to listen on) or NULL (listen on any)
	uint16_t port; //!< server port
	int timeout_ms; //!< 0 ar positive number
};

/**
 * @struct unhvd_hw_config
 * @brief Hardware decoder configuration.
 *
 * For more details see:
 * <a href="https://bmegli.github.io/hardware-video-decoder/structhvd__config.html">HVD documentation</a>
 *
 * @see unhvd_init
 */
struct unhvd_hw_config
{
	const char *hardware; //!< hardware type for decoding, e.g. "vaapi"
	const char *codec; //!< codec name, e.g. "h264", "vp8"
	const char *device; //!< NULL/empty string or device, e.g. "/dev/dri/renderD128"
	const char *pixel_format; //!< NULL for default or format, e.g. "rgb0", "bgr0", "nv12", "yuv420p"
	int width; //!< 0 to not specify, needed by some codecs
	int height; //!< 0 to not specify, needed by some codecs
	int profile; //!< 0 to leave as FF_PROFILE_UNKNOWN or profile e.g. FF_PROFILE_HEVC_MAIN, ...
};

/**
 * @struct unhvd_depth_config
 * @brief Depth unprojection configuration.
 *
 * For more details see:
 * <a href="https://github.com/bmegli/hardware-depth-unprojector">HDU</a>
 *
 * @see unhvd_init
 */
struct unhvd_depth_config
{
	float ppx; //!< principal point x pixel coordinates (center of projection)
	float ppy; //!< principal point y pixel coordinates (center of projection)
	float fx; //!< focal length in pixel width unit
	float fy; //!< focal length in pixel height unit
	float depth_unit; //!< multiplier for raw depth data;
	float min_margin; //!< minimal margin to treat as valid in result unit (raw data * depth_unit);
	float max_margin; //!< maximal margin to treat as valid in result unit (raw data * depth_unit);
};

enum UNHVD_COMPILE_TIME_CONSTANTS
{
	UNHVD_MAX_DECODERS = 3, //!< max number of decoders in multi-frame decoding
	UNHVD_NUM_DATA_POINTERS = 3 //!< max number of planes for planar image formats
};

/**
 * @struct unhvd_frame
 * @brief Video frame abstraction.
 *
 * Note that video data is often processed in multiplanar formats.
 * This may mean for example separate planes (arrays) for:
 * - luminance data
 * - color data
 *
 * @see unhvd_get_frame_begin, unhvd_get_frame_end, unhvd_get_begin, unhvd_get_end
 */
struct unhvd_frame
{
	int width; //!< width of frame in pixels
	int height; //!< height of frame in pixels
	int format; //!< FFmpeg pixel format
	uint8_t *data[UNHVD_NUM_DATA_POINTERS]; //!< array of pointers to frame planes (e.g. Y plane and UV plane)
	int linesize[UNHVD_NUM_DATA_POINTERS]; //!< array of strides of frame planes (row length including padding)
};

/**
  * @brief Vertex data (x, y, z)
  */
typedef float float3[3];

/**
  * @brief Vertex color data (rgba), currently only a as greyscale
  */
typedef uint32_t color32;

/**
  * @brief Quaternion data (x, y, z, w)
  */
typedef float float4[4];

/**
 * @struct unhvd_point_cloud
 * @brief Point cloud abstraction.
 *
 * Array of float3 points and color32 colors. Only used points are non zero.
 *
 * @see unhvd_get_point_cloud_begin, unhvd_get_point_cloud_end, unhvd_get_begin, unhvd_get_end
 */
struct unhvd_point_cloud
{
	float3 *data; //!< array of point coordinates
	color32 *colors; //!< array of point colors
	int size; //!< size of array
	int used; //!< number of elements used in array
	float3 position; //!< position vector XYZ from which point cloud was captured
	float4 rotation; //!< heading quaternion XYZW from which point cloud was captured
};

/**
  * @brief Constants returned by most of library functions
  */
enum unhvd_retval_enum
{
	UNHVD_ERROR=-1, //!< error occured
	UNHVD_OK=0, //!< succesfull execution
};

/**
 * @brief Initialize internal library data.
 *
 * Initialize streaming and single or multiple (hw_size > 1) hardware decoders.
 *
 * For video streaming the argument depth_config should be NULL.
 * Non NULL depth_config enables depth unprojection (point cloud streaming).
 *
 * @param net_config network configuration
 * @param hw_config hardware decoders configuration of hw_size size
 * @param hw_size number of supplied hardware decoder configurations
 * @param depth_config unprojection configuration (may be NULL)
 * @return
 * - pointer to internal library data
 * - NULL on error, errors printed to stderr
 *
 * @see unhvd_net_config, unhvd_hw_config, unhvd_depth_config
 */
UNHVD_EXPORT UNHVD_API struct unhvd *unhvd_init(
	const unhvd_net_config *net_config,
	const unhvd_hw_config *hw_config, int hw_size,
	const unhvd_depth_config *depth_config);

/**
 * @brief Free library resources
 *
 * Cleans and frees library memory.
 *
 * @param n pointer to internal library data
 * @see unhvd_init
 *
 */
UNHVD_EXPORT UNHVD_API void unhvd_close(unhvd *u);


/** @name Data retrieval functions
 *
 *  unhvd_xxx_begin functions should be always followed by corresponding unhvd_xxx_end calls.
 *  A mutex is held between begin and end function so be as fast as possible.
 *
 *  The ownership of the data remains with the library. You should consume the data immidiately
 *  (e.g. fill the texture, fill the vertex buffer). The data is valid only until call to corresponding end
 *  function.
 *
 *  The argument frame should point to single unhvd_frame or array of unhvd_frame
 *  if library was initialized for multiple hardware decoders with ::unhvd_init.
 *
 *  Functions will calculate point clouds from depth maps only if non NULL ::unhvd_depth_config was passed to ::unhvd_init
 *
 * @param n pointer to internal library data
 * @param frame pointer to frame description data (single or array)
 * @param pc pointer to point cloud description data
 * @return
 * - begin functions
 * 	- UNHVD_OK sucessfully returned new data
 * 	- UNHVD_ERROR no new data
 * - end functions
 *		- UNHVD_OK sucessfully finished begin/end block
 *		- UNHVD_ERROR fatal error occured
 *
 * @see unhvd_frame, unhvd_point_cloud
 *
 */
///@{
/** @brief Retrieve depth frame and point cloud.
 *
 * Point cloud data may be only retrieved if non NULL ::unhvd_depth_config was passed to ::unhvd_init.
 * This function may retrieve both depth frame and unprojected point cloud at the same time
 */
UNHVD_EXPORT UNHVD_API int unhvd_get_begin(unhvd *u, unhvd_frame *frame, unhvd_point_cloud *pc);
/** @brief Finish retrieval. */
UNHVD_EXPORT UNHVD_API int unhvd_get_end(unhvd *u);
/** @brief Retrieve video frame. */
UNHVD_EXPORT UNHVD_API int unhvd_get_frame_begin(unhvd *u, unhvd_frame *frame);
/** @brief Finish retrieval. */
UNHVD_EXPORT UNHVD_API int unhvd_get_frame_end(unhvd *u);
/** @brief Retrieve point cloud. */
UNHVD_EXPORT UNHVD_API int unhvd_get_point_cloud_begin(unhvd *u, unhvd_point_cloud *pc);
/** @brief Finish retrieval. */
UNHVD_EXPORT UNHVD_API int unhvd_get_point_cloud_end(unhvd *u);
///@}

/** @}*/
}

#endif
