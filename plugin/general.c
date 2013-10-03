/*
 * The general entry points for password strength checking.
 *
 * Provides the strength_init, strength_check, and strength_close entry points
 * for doing password strength checking.  These are the only interfaces that
 * are called by the implementation-specific code, and all other checks are
 * wrapped up in those interfaces.
 *
 * Developed by Derrick Brashear and Ken Hornstein of Sine Nomine Associates,
 *     on behalf of Stanford University.
 * Extensive modifications by Russ Allbery <rra@stanford.edu>
 * Copyright 2006, 2007, 2009, 2012, 2013
 *     The Board of Trustees of the Leland Stanford Junior Unversity
 *
 * See LICENSE for licensing terms.
 */

#include <config.h>
#include <portable/kadmin.h>
#include <portable/krb5.h>
#include <portable/system.h>

#ifdef HAVE_CDB_H
# include <cdb.h>
#endif
#include <ctype.h>
#include <errno.h>

#include <plugin/internal.h>
#include <util/macros.h>


/*
 * Initialize the module.  Ensure that the dictionary file exists and is
 * readable and store the path in the module context.  Returns 0 on success,
 * non-zero on failure.  This function returns failure only if it could not
 * allocate memory or internal Kerberos calls that shouldn't fail do.
 *
 * The dictionary file should not include the trailing .pwd extension.
 * Currently, we don't cope with a NULL dictionary path.
 */
krb5_error_code
strength_init(krb5_context ctx, const char *dictionary,
              krb5_pwqual_moddata *moddata)
{
    krb5_pwqual_moddata data = NULL;
    krb5_error_code code;

    /* Allocate our internal data. */
    data = calloc(1, sizeof(*data));
    if (data == NULL)
        return strength_error_system(ctx, "cannot allocate memory");
    data->cdb_fd = -1;

    /* Get minimum length information from krb5.conf. */
    strength_config_number(ctx, "minimum_length", &data->minimum_length);

    /* Get character class restrictions from krb5.conf. */
    strength_config_boolean(ctx, "require_ascii_printable", &data->ascii);
    strength_config_boolean(ctx, "require_non_letter", &data->nonletter);

    /*
     * Try to initialize CDB and CrackLib dictionaries.  Both functions handle
     * their own configuration parsing and will do nothing if the
     * corresponding dictionary is not configured.
     */
    code = strength_init_cracklib(ctx, data, dictionary);
    if (code != 0)
        goto fail;
    code = strength_init_cdb(ctx, data);
    if (code != 0)
        goto fail;

    /* Initialized.  Set moddata and return. */
    *moddata = data;
    return 0;

fail:
    if (data != NULL)
        strength_close(ctx, data);
    *moddata = NULL;
    return code;
}


/*
 * Check if a password contains only printable ASCII characters.
 */
static bool
only_printable_ascii(const char *password)
{
    const char *p;

    for (p = password; *p != '\0'; p++)
        if (!isascii((unsigned char) *p) || !isprint((unsigned char) *p))
            return false;
    return true;
}


/*
 * Check if a password contains only letters and spaces.
 */
static bool
only_alpha_space(const char *password)
{
    const char *p;

    for (p = password; *p != '\0'; p++)
        if (!isalpha((unsigned char) *p) && *p != ' ')
            return false;
    return true;
}


/*
 * Check a given password.  Takes a Kerberos context, our module data, the
 * password, the principal the password is for, and a buffer and buffer length
 * into which to put any failure message.
 */
krb5_error_code
strength_check(krb5_context ctx UNUSED, krb5_pwqual_moddata data,
               const char *password, const char *principal)
{
    char *user, *p;
    const char *q;
    size_t i, j;
    char c;
    krb5_error_code code;

    /* Check minimum length first, since that's easy. */
    if ((long) strlen(password) < data->minimum_length)
        return strength_error_tooshort(ctx, ERROR_SHORT);

    /*
     * If desired, check whether the password contains non-ASCII or
     * non-printable ASCII characters.
     */
    if (data->ascii && !only_printable_ascii(password))
        return strength_error_generic(ctx, ERROR_ASCII);

    /*
     * If desired, ensure the password has a non-letter (and non-space)
     * character.  This requires that people using phrases at least include a
     * digit or punctuation to make phrase dictionary attacks or dictionary
     * attacks via combinations of words harder.
     */
    if (data->nonletter && only_alpha_space(password))
        return strength_error_class(ctx, ERROR_LETTER);

    /*
     * We get the principal (in krb5_unparse_name format) and we want to be
     * sure that the password doesn't match the username, the username
     * reversed, or the username with trailing digits.  We therefore have to
     * copy the string so that we can manipulate it a bit.
     */
    if (strcasecmp(password, principal) == 0)
        return strength_error_generic(ctx, ERROR_USERNAME);
    user = strdup(principal);
    if (user == NULL)
        return strength_error_system(ctx, "cannot allocate memory");

    /* Strip the realm off of the principal. */
    for (p = user; p[0] != '\0'; p++) {
        if (p[0] == '\\' && p[1] != '\0') {
            p++;
            continue;
        }
        if (p[0] == '@') {
            p[0] = '\0';
            break;
        }
    }

    /*
     * If the length of the password matches the length of the local portion
     * of the principal, check for exact matches or reversed matches.
     */
    if (strlen(password) == strlen(user)) {
        if (strcasecmp(password, user) == 0) {
            free(user);
            return strength_error_generic(ctx, ERROR_USERNAME);
        }

        /* Check against the reversed username. */
        for (i = 0, j = strlen(user) - 1; i < j; i++, j--) {
            c = user[i];
            user[i] = user[j];
            user[j] = c;
        }
        if (strcasecmp(password, user) == 0) {
            free(user);
            return strength_error_generic(ctx, ERROR_USERNAME);
        }
    }

    /*
     * If the length is greater, check whether the user just added trailing
     * digits to the local portion of the principal to form the password.
     */
    if (strlen(password) > strlen(user))
        if (strncasecmp(password, user, strlen(user)) == 0) {
            q = password + strlen(user);
            while (isdigit((unsigned char) *q))
                q++;
            if (*q == '\0') {
                free(user);
                return strength_error_generic(ctx, ERROR_USERNAME);
            }
        }
    free(user);

    /* Check the password against CDB and CrackLib if configured. */
    code = strength_check_cracklib(ctx, data, password);
    if (code != 0)
        return code;
    code = strength_check_cdb(ctx, data, password);
    if (code != 0)
        return code;

    /* Success.  Password accepted. */
    return 0;
}


/*
 * Cleanly shut down the password strength plugin.  The only thing we have to
 * do is free the memory allocated for our internal data.
 */
void
strength_close(krb5_context ctx UNUSED, krb5_pwqual_moddata data)
{
    if (data != NULL) {
        strength_close_cdb(ctx, data);
        free(data->dictionary);
        free(data);
    }
}
