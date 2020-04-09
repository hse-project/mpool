/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_OMF_H
#define MPOOL_UTIL_OMF_H

#include <util/byteorder.h>
#include <util/inttypes.h>

#define BUILD_BUG_ON(_expr)     ((void)sizeof(char[1 - 2*!!(_expr)]))

/* The following two macros exist solely to enable the OMF_SETGET macros to
 * work on 8 bit members as well as 16, 32 and 64 bit members.
 */
#define le8_to_cpu(x)  (x)
#define cpu_to_le8(x)  (x)


/* Helper macro to define set/get methods for 8, 16, 32 or 64 bit
 * scalar OMF struct members.
 */
#define OMF_SETGET(type, member, bits) \
	OMF_SETGET2(type, member, bits, member)

#define OMF_SETGET2(type, member, bits, name)				\
	static __always_inline u##bits omf_##name(const type * s)	\
	{								\
		BUILD_BUG_ON(sizeof(((type *)0)->member)*8 != (bits));	\
		return le##bits##_to_cpu(s->member);			\
	}								\
	static __always_inline void omf_set_##name(type *s, u##bits val)\
	{								\
		s->member = cpu_to_le##bits(val);			\
	}

/* Helper macro to define set/get methods for character strings
 * embedded in OMF structures.
 */
#define OMF_SETGET_CHBUF(type, member) \
	OMF_SETGET_CHBUF2(type, member, member)

#define OMF_SETGET_CHBUF2(type, member, name)				\
	static inline void omf_set_##name(type *s, const void *p, size_t plen) \
	{								\
		size_t len = sizeof(((type *)0)->member);		\
		memcpy(s->member, p, len < plen ? len : plen);		\
	}								\
	static inline void omf_##name(const type *s, void *p, size_t plen)\
	{								\
		size_t len = sizeof(((type *)0)->member);		\
		memcpy(p, s->member, len < plen ? len : plen);		\
	}

#define OMF_GET_VER(type, member, bits, ver)                            \
	static __always_inline u##bits omf_##member##_##ver(const type *s)    \
	{                                                               \
		BUILD_BUG_ON(sizeof(((type *)0)->member)*8 != (bits));	\
		return le##bits##_to_cpu(s->member);			\
	}

#define OMF_GET_CHBUF_VER(type, member, ver)                                   \
	static inline void omf_##member##_##ver(const type *s,                 \
						void *p, size_t plen)          \
	{							               \
		size_t len = sizeof(((type *)0)->member);                      \
		memcpy(p, s->member, len < plen ? len : plen);                 \
	}

#endif /* MPOOL_UTIL_OMF_H */
