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
