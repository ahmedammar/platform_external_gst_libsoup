LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LIBSOUP_TOP := $(LOCAL_PATH)

LIBSOUP_BUILT_SOURCES :=	\
	libsoup/Android.mk

LIBSOUP_BUILT_SOURCES := $(patsubst %, $(abspath $(LIBSOUP_TOP))/%, $(LIBSOUP_BUILT_SOURCES))

.PHONY: libsoup-configure
libsoup-configure:
	cd $(LIBSOUP_TOP) ; \
	NOCONFIGURE=1 \
	$(abspath $(LIBSOUP_TOP))/autogen.sh && \
	cp ../config.sub . && \
	autoreconf && \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	XML_CFLAGS="-I$(abspath $(TOP)/external/libxml2/include/) -I$(abspath $(TOP)/external/icu4c/common)" \
	XML_LIBS="-lxml2" \
	$(abspath $(LIBSOUP_TOP))/configure --host=arm-linux-androideabi \
	--prefix=/system --without-gnome --disable-gtk-doc PACKAGE=libsoup && \
	for file in $(LIBSOUP_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

CONFIGURE_TARGETS += libsoup-configure

-include $(LIBSOUP_TOP)/libsoup/Android.mk
