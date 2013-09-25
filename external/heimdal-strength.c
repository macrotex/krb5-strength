/*
 * Password strength checking program for Heimdal.
 *
 * This is a wrapper around the krb5-strength-modified version of CrackLib
 * that supports the Heimdal external password strength check program
 * interface.  It uses a krb5.conf parameter to determine the location of its
 * dictionary.
 *
 * Written by Russ Allbery <rra@stanford.edu>
 * Copyright 2009, 2013
 *     The Board of Trustees of the Leland Stanford Junior University
 *
 * See LICENSE for licensing terms.
 */

#include <config.h>
#include <portable/krb5.h>
#include <portable/system.h>

#include <errno.h>

#include <plugin/api.h>
#include <util/macros.h>
#include <util/messages.h>
#include <util/xmalloc.h>


/*
 * Read a key/value pair from stdin, check that the key is the one expected,
 * and if so, copy the value into the provided buffer.  Exits with an
 * appropriate error on failure.
 */
static void
read_key(const char *key, char *buffer, size_t length)
{
    char *p;

    if (fgets(buffer, length, stdin) == NULL)
        sysdie("Cannot read %s", key);
    if (strlen(buffer) < 1 || buffer[strlen(buffer) - 1] != '\n')
        die("Malformed or too long %s line", key);
    buffer[strlen(buffer) - 1] = '\0';
    if (strncmp(buffer, key, strlen(key)) != 0)
        die("Malformed %s line", key);
    p = buffer + strlen(key);
    if (p[0] != ':' || p[1] != ' ')
        die("Malformed %s line", key);
    p += 2;
    memmove(buffer, p, strlen(p) + 1);
}


/*
 * Read a principal and password from standard input and do strength checking
 * on that principal and password, returning the results expected by the
 * Heimdal external-check interface.  Takes the password strength checking
 * context.
 */
static void
check_password(krb5_context ctx, krb5_pwqual_moddata data)
{
    char principal[BUFSIZ], password[BUFSIZ], error[BUFSIZ], end[BUFSIZ];

    read_key("principal", principal, sizeof(principal));
    read_key("new-password", password, sizeof(password));
    if (fgets(end, sizeof(end), stdin) == NULL)
        sysdie("Cannot read end of entry");
    if (strcmp(end, "end\n") != 0)
        die("Malformed end line");
    if (pwcheck_check(ctx, data, password, principal, error, sizeof(error)))
        fprintf(stderr, "%s\n", error);
    else
        printf("APPROVED\n");
}


/*
 * Main routine.  There will be one argument, the principal, but we ignore it
 * (we get it again via the input data).
 *
 * Heimdal 1.3 appears to pass the principal as argv[0], where the name of the
 * program would normally be, so allow for that behavior as well.
 */
int
main(int argc, char *argv[] UNUSED)
{
    krb5_context ctx;
    krb5_pwqual_moddata data;

    /* Check command-line arguments. */
    if (argc != 1 && argc != 2)
        die("Usage: heimdal-strength <principal>");

    /* Initialize Kerberos and the module. */
    if (krb5_init_context(&ctx) != 0)
        die("Cannot create Kerberos context");
    if (pwcheck_init(ctx, NULL, &data) != 0)
        die("cannot initialize strength checking");

    /* Check the password and report results. */
    check_password(ctx, data);

    /* Close and free resources. */
    pwcheck_close(ctx, data);
    krb5_free_context(ctx);
    exit(0);
}
