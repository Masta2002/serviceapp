LANGS = ar bg bs cs da de el en es fa fi fr hr hu it lv nb nl pl pt pt_BR ro ru sk sr sv tr uk zh_CN
LANGMO = $(LANGS:=.mo)
LANGPO = $(LANGS:=.po)

EXTRA_DIST = $(LANGPO) ServiceApp.pot updateallpo.sh

CLEANFILES = $(LANGMO)

.po.mo:
	$(MSGFMT) -o $@ $<

BUILT_SOURCES = $(LANGMO)

dist-hook: $(LANGPO)

install-data-local: $(LANGMO)
	for lang in $(LANGS); do \
		$(mkinstalldirs) $(DESTDIR)${libdir}/enigma2/python/Plugins/SystemPlugins/ServiceApp/locale/$$lang/LC_MESSAGES; \
		$(INSTALL_DATA) $$lang.mo $(DESTDIR)${libdir}/enigma2/python/Plugins/SystemPlugins/ServiceApp/locale/$$lang/LC_MESSAGES/ServiceApp.mo; \
	done

uninstall-local:
	for lang in $(LANGS); do \
		$(RM) $(DESTDIR)${libdir}/enigma2/python/Plugins/SystemPlugins/ServiceApp/locale/$$lang/LC_MESSAGES/ServiceApp.mo; \
	done
