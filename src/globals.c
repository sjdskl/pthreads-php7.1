/*
  +----------------------------------------------------------------------+
  | pthreads                                                             |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2012 - 2015                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Joe Watkins <krakjoe@php.net>                                |
  +----------------------------------------------------------------------+
 */
#ifndef HAVE_PTHREADS_GLOBALS
#define HAVE_PTHREADS_GLOBALS

#ifndef HAVE_PTHREADS_GLOBALS_H
#	include <src/globals.h>
#endif

struct _pthreads_globals pthreads_globals;

#ifndef PTHREADS_G
#	define PTHREADS_G () ?  : (void***) &pthreads_globals
#endif

extern int pthreads_connect(pthreads_object_t* source, pthreads_object_t* destination);

/* {{{ */
zend_bool pthreads_globals_init(){
	if (!PTHREADS_G(init)&&!PTHREADS_G(failed)) {
		PTHREADS_G(init)=1;
		if (!(PTHREADS_G(monitor)=pthreads_monitor_alloc()))
			PTHREADS_G(failed)=1;
		if (PTHREADS_G(failed)) {
		    PTHREADS_G(init)=0;
		} else {
		    zend_hash_init(
		    	&PTHREADS_G(objects), 64, NULL, (dtor_func_t) NULL, 1);
			zend_hash_init(
				&PTHREADS_G(gstrings), 64, NULL, (dtor_func_t) NULL, 1);
		}

#define INIT_STRING(n, v) do { \
	PTHREADS_G(strings).n = zend_string_init(v, 1); \
	GC_REFCOUNT(PTHREADS_G(strings).n)++; \
} while(0)
		INIT_STRING(run, ZEND_STRL("run"));
		INIT_STRING(session.cache_limiter, ZEND_STRL("cache_limiter"));
		INIT_STRING(session.use_cookies, ZEND_STRL("use_cookies"));
#undef INIT_STRING

		ZVAL_NEW_STR(
			&PTHREADS_G(strings).worker, 
			zend_string_init(ZEND_STRL("worker"), 1));
		Z_ADDREF(PTHREADS_G(strings).worker);

		return PTHREADS_G(init);
	} else return 0;
} /* }}} */

/* {{{ */
zend_bool pthreads_globals_lock(){
	return pthreads_monitor_lock(PTHREADS_G(monitor));
} /* }}} */

/* {{{ */
void pthreads_globals_unlock() {
	pthreads_monitor_unlock(PTHREADS_G(monitor));
} /* }}} */

/* {{{ */
void* pthreads_globals_object_alloc(size_t length) {
	zend_bool locked = 0;
	void *bucket     = (void*) ecalloc(1, length);

	if (pthreads_globals_lock()) {
		zend_hash_index_update_ptr(
			&PTHREADS_G(objects),
			(zend_ulong) bucket, bucket);
		pthreads_globals_unlock();
	}

	memset(bucket, 0, length);
	
	return bucket;
} /* }}} */

/* {{{ */
zend_bool pthreads_globals_object_connect(zend_ulong address, zend_class_entry *ce, zval *object) {
	zend_bool valid = 0;

	if (!address)
		return valid;

	if (pthreads_globals_lock()) {
		if (zend_hash_index_exists(&PTHREADS_G(objects), address)) {
			valid = 1;
		}
		pthreads_globals_unlock();
	}

	if (valid) {
		
		/*
		* This can be done outside of a critical section because there are only two possibilities:
		*	We own the object: no possible pathway to fault (read free'd memory)
		*	We don't own the object: possibly pathway to fault whether we use critical section or not:
		*		We use a critical section: we create the connection knowing that address cannot be freed while doing so
		*		however, as soon as we leave the section, and before the conext that called this routine can reference the connection
		*		object the creating context may have free'd the object.
		*		We don't use a critical section: the object may be freed while we are creating the connection, causing a fault.
		* 
		* As always, it's necessary for the programmer to retain the appropriate references so that this does not fault, creating connections
		* in a critical section would be unecessarily slow, not to mention recursively lock mutex (which is fine, but not ideal).
		*/

		if (PTHREADS_IN_CREATOR(((pthreads_object_t*)address))) {
			/* we own the object in this context */
			ZVAL_OBJ(object, &((pthreads_object_t*)address)->std);
			Z_ADDREF_P(object);
		} else {
			/* we do not own the object, create a connection */
			if (!ce) {
				/* we may not know the class, can't use ce directly
					from zend_object because it is from another context */
				ce = zend_lookup_class(
					((pthreads_object_t*)address)->std.ce->name);
			}
			object_init_ex(object, ce);
			pthreads_connect(
				(pthreads_object_t*) address,
				PTHREADS_FETCH_FROM(Z_OBJ_P(object)));
		}
	}
	
	return valid;
} /* }}} */

/* {{{ */
zend_string* pthreads_globals_string(zend_string *str) {
	/*
	 This is sourcery of the darkest kind ...
	
	 We don't need all threads to have a copy of every string used as the name for a threaded objects property.
	 
	 In addition, Zend has troubles with persistent strings in non-persistent hashes and vice versa, to avoid these
	 we have a global table of strings that is used when writing to threaded objects properties, this table is detroyed
	 on shutdown of the process, so will show as leaked, but we don't care, we want stable !
	*/
	zend_string* p = NULL;
	if (pthreads_globals_lock()) {
		if (!(p = zend_hash_find_ptr(&PTHREADS_G(gstrings), str)) && 
			(p = (zend_string*) malloc(ZEND_MM_ALIGNED_SIZE(_ZSTR_STRUCT_SIZE(ZSTR_LEN(str)))))) {
			memset(p, 0, sizeof(zend_string));

			GC_REFCOUNT(p) = 2;
			GC_TYPE_INFO(p) = IS_STR_PERSISTENT;

			memcpy(ZSTR_VAL(p), ZSTR_VAL(str), ZSTR_LEN(str));
			p->len = ZSTR_LEN(str);
			ZSTR_VAL(p)[ZSTR_LEN(p)] = '\0';
			zend_string_forget_hash_val(p);
			zend_hash_update_ptr(
				&PTHREADS_G(gstrings), p, p);
		}

		pthreads_globals_unlock();
	}

	return p;
} /* }}} */

/* {{{ */
zend_bool pthreads_globals_object_delete(void *address) {
	zend_bool deleted = 0;
	
	if (!address)
		return deleted;

	if (pthreads_globals_lock()) {
		deleted = zend_hash_index_del(
			&PTHREADS_G(objects), (zend_ulong) address);
		pthreads_globals_unlock();
	}
	
	return deleted;
} /* }}} */

/* {{{ */
void pthreads_globals_shutdown() {
	if (PTHREADS_G(init)) {
		PTHREADS_G(init)=0;
		PTHREADS_G(failed)=0;
		/* we allow proc shutdown to destroy tables, and global strings */
		pthreads_monitor_free(PTHREADS_G(monitor));
	}
} /* }}} */
#endif
