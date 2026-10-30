SUBDIRS = rbit rbwm rvi cowsay cat true fetch rtop cc tests

# NO_TESTS=1 builds/installs only the normal userspace, not the autotests.
# Used by `make filesystem NO_TESTS=1` for a lean deployment image.
NO_TESTS ?= 0
ifeq ($(NO_TESTS),1)
SUBDIRS := $(filter-out tests,$(SUBDIRS))
endif

.PHONY: all clean install uninstall $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

install:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done

uninstall:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir uninstall; \
	done

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
