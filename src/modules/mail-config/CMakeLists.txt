set(extra_deps
	email-engine
	evolution-mail
)
set(sources
	evolution-mail-config.c
	e-mail-config-local-accounts.c
	e-mail-config-remote-accounts.c
	e-mail-config-imapx-options.c
	e-mail-config-imapx-options.h
	e-mail-config-sendmail-backend.c
	e-mail-config-sendmail-backend.h
	e-mail-config-smtp-backend.c
	e-mail-config-smtp-backend.h
	e-mail-config-google-summary.c
	e-mail-config-google-summary.h
	e-mail-config-yahoo-summary.c
	e-mail-config-yahoo-summary.h
)
set(extra_defines)
set(extra_cflags)
set(extra_incdirs)
set(extra_ldflags)

add_evolution_module(module-mail-config
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)
