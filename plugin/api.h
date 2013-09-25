/*
 * Prototypes for the kadmin password strength checking plugin.
 *
 * Developed by Derrick Brashear and Ken Hornstein of Sine Nomine Associates,
 *     on behalf of Stanford University.
 * Extensive modifications by Russ Allbery <rra@stanford.edu>
 * Copyright 2006, 2007, 2009, 2012, 2013
 *     The Board of Trustees of the Leland Stanford Junior Unversity
 *
 * See LICENSE for licensing terms.
 */

#ifndef PLUGIN_INTERNAL_H
#define PLUGIN_INTERNAL_H 1

#include <config.h>
#include <portable/krb5.h>

#ifdef HAVE_KRB5_PWQUAL_PLUGIN_H
# include <krb5/pwqual_plugin.h>
#else
typedef struct krb5_pwqual_moddata_st *krb5_pwqual_moddata;
#endif

/*
 * MIT Kerberos uses this type as an abstract data type for any data that a
 * password quality check needs to carry.  Reuse it since then we get type
 * checking for at least the MIT plugin.
 */
struct krb5_pwqual_moddata_st {
    char *dictionary;
};

/* Initialize the plugin and set up configuration. */
krb5_error_code pwcheck_init(krb5_context, const char *dictionary,
                             krb5_pwqual_moddata *);

/*
 * Check a password.  Returns 0 if okay, non-zero on rejection, and stores
 * the rejection method in the provided errstr buffer.
 */
krb5_error_code pwcheck_check(krb5_context, krb5_pwqual_moddata,
                              const char *password, const char *principal,
                              char *errstr, int errstrlen);

/* Finished checking passwords.  Free internal data. */
void pwcheck_close(krb5_context, krb5_pwqual_moddata);

#endif /* !PLUGIN_INTERNAL_H */
