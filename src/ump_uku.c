/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *       http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file ump_uku.c
 * File implements the user side of the user-kernel interface
 */

#include "ump.h"
#include "ump_ukk.h"
#include <stdio.h>
#include "ump_ioctl.h"

#include <sys/mman.h>

/* Needed for file operations on the device file*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

static _ump_osu_errcode_t ump_driver_ioctl(void *context, u32 command, void *args);

static int ump_ioctl_api_version_used = UMP_IOCTL_API_VERSION;

/**
 * The device file to access the UMP device driver
 * This is a character special file giving access to the device driver.
 * Usually created using the mknod command line utility.
 */
static const char ump_device_file_name[] = "/dev/ump";

_ump_osu_errcode_t _ump_ukk_open( void **context )
{
	long ump_device_file;
	if(NULL == context)
	{
		return _UMP_OSU_ERR_FAULT;
	}

	ump_device_file = open(ump_device_file_name, O_RDWR);

	if (-1 == ump_device_file)
	{
		return _UMP_OSU_ERR_FAULT;
	}

	{
		struct _ump_uk_api_version_s args;
		args.ctx     = (void*)ump_device_file;
		args.version = UMP_IOCTL_API_VERSION;
		args.compatible = 3;
		ump_driver_ioctl(args.ctx, UMP_IOC_QUERY_API_VERSION, &args);
		if ( 1 != args.compatible )
		{
			if (IS_API_MATCH(MAKE_VERSION_ID(1), args.version))
			{
				ump_ioctl_api_version_used = MAKE_VERSION_ID(1);
				UMP_PRINTF("The UMP devicedriver does not support cached UMP. Update it if this is needed.\n");
			}
			else
			{
				UMP_PRINTF("The UMP devicedriver is version: %d, UMP libraries is version: %d.\n", GET_VERSION(args.version), GET_VERSION(UMP_IOCTL_API_VERSION) );
			   close(ump_device_file);
			   return _UMP_OSU_ERR_FAULT;
			}
		}
	}

	*context = (void *) ump_device_file;
	return _UMP_OSU_ERR_OK;
}

_ump_osu_errcode_t _ump_ukk_close( void **context )
{
	if(NULL == context)
	{
		return _UMP_OSU_ERR_FAULT;
	}

	if(-1 == (long)*context)
	{
		return _UMP_OSU_ERR_FAULT;
	}

	close((long)*context);
	*context = (void *)-1;

	return _UMP_OSU_ERR_OK;
}

int _ump_ukk_allocate(_ump_uk_allocate_s *args)
{
	return ump_driver_ioctl(args->ctx, UMP_IOC_ALLOCATE, args);
}

_ump_osu_errcode_t _ump_ukk_release(_ump_uk_release_s *args)
{
	return ump_driver_ioctl(args->ctx, UMP_IOC_RELEASE, args);
}

_ump_osu_errcode_t _ump_ukk_size_get(_ump_uk_size_get_s *args)
{
	return ump_driver_ioctl(args->ctx, UMP_IOC_SIZE_GET, args);
}

_ump_osu_errcode_t _ump_ukk_phys_addr_get( _ump_uk_phys_addr_get_s *args )
{
	return ump_driver_ioctl(args->ctx, UMP_IOC_PHYS_ADDR_GET, args);
}

void _ump_ukk_msynch(_ump_uk_msync_s *args)
{
	/* This is for backwards compatibillity */
	if ( MAKE_VERSION_ID(1) == ump_ioctl_api_version_used)
	{
		args->is_cached = 0;
		if ( _UMP_UK_MSYNC_READOUT_CACHE_ENABLED != args->op )
		{
			UMP_DEBUG_PRINT(3, ("Warning: Doing UMP cache flush operations on a Device Driver that does not support cached UMP mem.\n"));
		}
		return;
	}
	ump_driver_ioctl(args->ctx, UMP_IOC_MSYNC, args);
}

#if UNIFIED_MEMORY_PROVIDER_VERSION > 2
void _ump_ukk_cache_operations_control( _ump_uk_cache_operations_control_s *args )
{
	ump_driver_ioctl(args->ctx, UMP_IOC_CACHE_OPERATIONS_CONTROL, args);
}

void _ump_ukk_switch_hw_usage( _ump_uk_switch_hw_usage_s *args )
{
	ump_driver_ioctl(args->ctx, UMP_IOC_SWITCH_HW_USAGE, args);
}

void _ump_ukk_lock( _ump_uk_lock_s *args )
{
	ump_driver_ioctl(args->ctx, UMP_IOC_LOCK, args);
}

void _ump_ukk_unlock( _ump_uk_unlock_s *args )
{
	ump_driver_ioctl(args->ctx, UMP_IOC_UNLOCK, args);
}
#endif /* UNIFIED_MEMORY_PROVIDER_VERSION */

int _ump_ukk_map_mem(_ump_uk_map_mem_s *args)
{
	int flags;
	if( -1 == (long)args->ctx )
	{
		return -1;
	}

	flags = MAP_SHARED;

	/* This is for backwards compatibillity */
	if ( MAKE_VERSION_ID(1) == ump_ioctl_api_version_used)
	{
		args->is_cached = 0;
	}

	/* If we want the Caching to be enabled we set the flags to be PRIVATE. The UMP DD reads this and do proper handling
	   Note: this enforces the user to use proper invalidation*/
	if ( args->is_cached ) flags = MAP_PRIVATE;

	args->mapping = mmap(NULL, args->size, PROT_READ | PROT_WRITE ,flags , (long)args->ctx, (off_t)args->secure_id * sysconf(_SC_PAGE_SIZE));
	if (MAP_FAILED == args->mapping)
	{
		return -1;
	}

    args->cookie = 0; /* Cookie is not used in linux _ump_ukk_unmap_mem */

	return 0;
}

void _ump_ukk_unmap_mem( _ump_uk_unmap_mem_s *args )
{
	/*
	 * If a smaller size is used Linux will just remove the requested range but don't tell
	 * the ump driver before all of it is unmapped, either via another unmap request or upon process shutdown.
	 * Unmapping too much will just ignore the overhead or hit undefined behavior,
	 * only affecting the calling process which could mess itself up in other ways anyway.
	 * So we don't need any security checks here.
	 */
	munmap(args->mapping, args->size);
}

static _ump_osu_errcode_t ump_driver_ioctl(void *context, u32 command, void *args)
{
	/*UMP_CHECK_NON_NULL(args, _UMP_OSK_ERR_INVALID_ARGS);*/

   	/* check for a valid file descriptor */
	/** @note manual type safety check-point */
	if( -1 == (long)context )
	{
		return _UMP_OSU_ERR_FAULT;
	}

	/* call ioctl handler of driver */
	if (0 != ioctl((long)context, command, args)) return -1;
	return _UMP_OSU_ERR_OK;
}

_ump_osu_errcode_t _ump_ukk_dmabuf_import( _ump_uk_dmabuf_s *args )
{
	return ump_driver_ioctl(args->ctx, UMP_IOC_DMABUF_IMPORT, args);
}
