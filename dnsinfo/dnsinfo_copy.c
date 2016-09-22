/*
 * Copyright (c) 2004, 2006, 2008-2013, 2015, 2016 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

/*
 * Modification History
 *
 * March 9, 2004		Allan Nathanson <ajn@apple.com>
 * - initial revision
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <os/activity.h>
#include <xpc/xpc.h>

#include "libSystemConfiguration_client.h"
#include "dnsinfo.h"
#include "dnsinfo_private.h"
#include "dnsinfo_internal.h"


const char *
dns_configuration_notify_key()
{
	const char	*key;

	key = "com.apple.system.SystemConfiguration.dns_configuration";
	return key;
}


#pragma mark -
#pragma mark DNS configuration [dnsinfo] client support


// Note: protected by __dns_configuration_queue()
static int			dnsinfo_active		= 0;
static libSC_info_client_t	*dnsinfo_client		= NULL;


static os_activity_t
__dns_configuration_activity()
{
	static os_activity_t	activity;
	static dispatch_once_t  once;

	dispatch_once(&once, ^{
		activity = os_activity_create("accessing DNS configuration",
					      OS_ACTIVITY_CURRENT,
					      OS_ACTIVITY_FLAG_DEFAULT);
	});

	return activity;
}


static dispatch_queue_t
__dns_configuration_queue()
{
	static dispatch_once_t  once;
	static dispatch_queue_t q;

	dispatch_once(&once, ^{
		q = dispatch_queue_create(DNSINFO_SERVICE_NAME, NULL);
	});

	return q;
}


dns_config_t *
dns_configuration_copy()
{
	uint8_t			*buf		= NULL;
	size_t			bufLen;
	dns_config_t		*config		= NULL;
	static const char	*proc_name	= NULL;
	xpc_object_t		reqdict;
	xpc_object_t		reply;

	if (!libSC_info_available()) {
		os_log(OS_LOG_DEFAULT, "*** DNS configuration requested between fork() and exec()");
		return NULL;
	}

	dispatch_sync(__dns_configuration_queue(), ^{
		if ((dnsinfo_active++ == 0) || (dnsinfo_client == NULL)) {
			static dispatch_once_t	once;
			static const char	*service_name	= DNSINFO_SERVICE_NAME;

			dispatch_once(&once, ^{
#if	DEBUG
				const char	*name;

				// get [XPC] service name
				name = getenv(service_name);
				if (name != NULL) {
					service_name = strdup(name);
				}
#endif	// DEBUG

				// get process name
				proc_name = getprogname();
			});

			dnsinfo_client =
				libSC_info_client_create(__dns_configuration_queue(),	// dispatch queue
							 service_name,			// XPC service name
							 "DNS configuration");		// service description
			if (dnsinfo_client == NULL) {
				--dnsinfo_active;
			}
		}
	});

	if ((dnsinfo_client == NULL) || !dnsinfo_client->active) {
		// if DNS configuration server not available
		return NULL;
	}

	// scope DNS configuration activity
	os_activity_scope(__dns_configuration_activity());

	// create message
	reqdict = xpc_dictionary_create(NULL, NULL, 0);

	// set process name
	if (proc_name != NULL) {
		xpc_dictionary_set_string(reqdict, DNSINFO_PROC_NAME, proc_name);
	}

	// set request
	xpc_dictionary_set_int64(reqdict, DNSINFO_REQUEST, DNSINFO_REQUEST_COPY);

	// send request to the DNS configuration server
	reply = libSC_send_message_with_reply_sync(dnsinfo_client, reqdict);
	xpc_release(reqdict);

	if (reply != NULL) {
		const void	*dataRef;
		size_t		dataLen	= 0;

		dataRef = xpc_dictionary_get_data(reply, DNSINFO_CONFIGURATION, &dataLen);
		if ((dataRef != NULL) &&
		    ((dataLen >= sizeof(_dns_config_buf_t)) && (dataLen <= DNS_CONFIG_BUF_MAX))) {
			_dns_config_buf_t       *config         = (_dns_config_buf_t *)(void *)dataRef;
			size_t			configLen;
			uint32_t                n_attribute	= ntohl(config->n_attribute);
			uint32_t		n_padding       = ntohl(config->n_padding);

			/*
			 * Check that the size of the configuration header plus the size of the
			 * attribute data matches the size of the configuration buffer.
			 *
			 * If the sizes are different, something that should NEVER happen, CRASH!
			 */
			configLen = sizeof(_dns_config_buf_t) + n_attribute;
			assert(configLen == dataLen);

			/*
			 * Check that the size of the requested padding would not result in our
			 * allocating a configuration + padding buffer larger than our maximum size.
			 *
			 * If the requested padding size is too large, something that should NEVER
			 * happen, CRASH!
			 */
			assert(n_padding <= (DNS_CONFIG_BUF_MAX - dataLen));

			/*
			 * Check that the actual size of the configuration data and any requested
			 * padding will be less than the maximum possible size of the in-memory
			 * configuration buffer.
			 *
			 * If the length needed is too large, something that should NEVER happen, CRASH!
			 */
			bufLen = dataLen + n_padding;
			assert(bufLen <= DNS_CONFIG_BUF_MAX);

			// allocate a buffer large enough to hold both the configuration
			// data and the padding.
			buf = malloc(bufLen);
			bcopy((void *)dataRef, buf, dataLen);
			bzero(&buf[dataLen], n_padding);
		}

		xpc_release(reply);
	}

	if (buf != NULL) {
		/* ALIGN: cast okay since _dns_config_buf_t is int aligned */
		config = _dns_configuration_expand_config((_dns_config_buf_t *)(void *)buf);
		if (config == NULL) {
			free(buf);
		}
	}

	return config;
}


void
dns_configuration_free(dns_config_t *config)
{
	if (config == NULL) {
		return;	// ASSERT
	}

	dispatch_sync(__dns_configuration_queue(), ^{
		if (--dnsinfo_active == 0) {
			// if last reference, drop connection
			libSC_info_client_release(dnsinfo_client);
			dnsinfo_client = NULL;
		}
	});

	free((void *)config);
	return;
}


void
_dns_configuration_ack(dns_config_t *config, const char *bundle_id)
{
	xpc_object_t	reqdict;

	if (config == NULL) {
		return;	// ASSERT
	}

	if ((dnsinfo_client == NULL) || !dnsinfo_client->active) {
		// if DNS configuration server not available
		return;
	}

	dispatch_sync(__dns_configuration_queue(), ^{
		dnsinfo_active++;	// keep connection active (for the life of the process)
	});

	// create message
	reqdict = xpc_dictionary_create(NULL, NULL, 0);

	// set request
	xpc_dictionary_set_int64(reqdict, DNSINFO_REQUEST, DNSINFO_REQUEST_ACKNOWLEDGE);

	// set generation
	xpc_dictionary_set_uint64(reqdict, DNSINFO_GENERATION, config->generation);

	// send acknowledgement to the DNS configuration server
	xpc_connection_send_message(dnsinfo_client->connection, reqdict);

	xpc_release(reqdict);
	return;
}

#ifdef MAIN

int
main(int argc, char **argv)
{
	dns_config_t	*config;

	config = dns_configuration_copy();
	if (config != NULL) {
		dns_configuration_free(config);
	}

	exit(0);
}

#endif
