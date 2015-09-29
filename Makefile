default:
	$(MAKE) -C drivers/platform/x86 $@

install:
	$(MAKE) -C drivers/platform/x86 install $@

clean:
	$(MAKE) -C drivers/platform/x86 clean $@
