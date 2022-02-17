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

#ifndef	_MORE_H
#define	_MORE_H

#include <libnvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct node_sysevent node_sysevent_t;

typedef void (nsev_callback_t)(nvlist_t *, nvlist_t *, void *);

int nsev_init(void);

int nsev_attach(nsev_callback_t *, void *, node_sysevent_t **);
void nsev_detach(node_sysevent_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* !_MORE_H */
