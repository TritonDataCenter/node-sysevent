#ifndef	_CROSSTHREAD_H
#define	_CROSSTHREAD_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef void (crossthread_func_t)(void *, void *);

int crossthread_invoke(crossthread_func_t *, void *, void *);
int crossthread_init(void);

void crossthread_take_hold(void);
void crossthread_release_hold(void);

#ifdef	__cplusplus
}
#endif

#endif	/* !_CROSSTHREAD_H */
