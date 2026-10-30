SUBDIRS = tltest abitest syscalltest devtest fstest memtest fdtest pthreadtest futextest robusttest stacktest jobtest setuidtest prctltest statxtest proctest openat2test clone3test rseqtest linuxtest crashtest fbtest vttest pledgetest randomtest headertest unveiltest sysctltest rbtest

.PHONY: all clean install uninstall $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

install:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

uninstall:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir uninstall; \
	done
