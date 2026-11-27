import locale
from gettext import bindtextdomain, dgettext, dngettext, gettext, ngettext

from Components.Language import language
from Tools.Directories import resolveFilename, SCOPE_PLUGINS


PluginLanguageDomain = "ServiceApp"


def localeInit():
	path = resolveFilename(SCOPE_PLUGINS, "SystemPlugins/ServiceApp/locale")
	bindtextdomain(PluginLanguageDomain, path)
	# the C++ part (SAPP_TR) translates with libc dgettext: gettext.bindtextdomain
	# above is pure Python and does not reach it, locale.bindtextdomain does
	try:
		locale.bindtextdomain(PluginLanguageDomain, path)
	except (AttributeError, OSError):
		pass


def _(txt):
	t = dgettext(PluginLanguageDomain, txt)
	if t == txt:
		t = gettext(txt)
	return t


def _ngettext(singular, plural, n):
	trans = dngettext(PluginLanguageDomain, singular, plural, n)
	if trans in (singular, plural):
		trans = ngettext(singular, plural, n)
	return trans


localeInit()
language.addCallback(localeInit)
