#!/usr/bin/env node
/* vim: set ts=8 sts=8 sw=8 noet: */

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2022 Joyent, Inc.
 */

var mod_stream = require('stream');

var SyseventImpl = require('bindings')('module').SyseventImpl;

var NEXT_ID = 1;
var STREAMS = [];

var SYSEVENT_IMPL = null;

function
ensureImplExists()
{
	if (SYSEVENT_IMPL !== null) {
		return;
	}

	SYSEVENT_IMPL = new SyseventImpl(function (nvl0, nvl1) {
		STREAMS.forEach(function (s) {
			s.push({
				nvl0: nvl0,
				nvl1: nvl1
			});
		});
	});
}

function
checkImplStillNeeded()
{
	if (SYSEVENT_IMPL === null) {
		return;
	}

	if (STREAMS.length === 0) {
		SYSEVENT_IMPL.destroy();
		SYSEVENT_IMPL = null;
	}
}

function
createSyseventStream()
{
	ensureImplExists();

	var s = new mod_stream.Readable({
		objectMode: true
	});
	s._stream_id = NEXT_ID++;
	s._read = function () {};
	s.destroy = function () {
		for (var i = 0; i < STREAMS.length; i++) {
			if (STREAMS[i]._stream_id === s._stream_id) {
				STREAMS.splice(i, 1);
				break;
			}
		}
		checkImplStillNeeded();
	};
	STREAMS.push(s);

	return (s);
}

module.exports = {
	createSyseventStream: createSyseventStream
};
