/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | http://www.opensource.org/licenses/mit-license.php  MIT License      |
  +----------------------------------------------------------------------+
  | Author: Jani Taskinen <jani.taskinen@iki.fi>                         |
  | Author: Patrick Reilly <preilly@php.net>                             |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

extern "C" {
#include "php.h"
#include "zend_exceptions.h"
}

#include "php_v8js_macros.h"

void php_v8js_amd_split_terms(char *input, std::vector<const char *> &terms)
{
    char *term = (char *)malloc(PATH_MAX), *ptr = (char *)term;

    // Initialise the term string
    *term = 0;

    while (*input > 0) {
        if (*input == '/') {
            if (strlen(term) > 0) {
                // Terminate term string and add to terms vector
                *ptr++ = 0;
                terms.push_back(strdup(term));

                // Reset term string
                memset(term, 0, strlen(term));
                ptr = term;
            }
        } else {
            *ptr++ = *input;
        }

        input++;
    }

    if (strlen(term) > 0) {
        // Terminate term string and add to terms vector
        *ptr++ = 0;
        terms.push_back(strdup(term));
    }

    if (term > 0) {
        free(term);
    }
}

void php_v8js_amd_normalise_id(const char *base_module_id, const char *module_id, char *normalised_module_id)
{
    std::vector<const char *> id_terms, terms;
    php_v8js_amd_split_terms((char *)module_id, id_terms);

    // If we have a relative module ID then include the base module ID
    if (!strcmp(id_terms.front(), ".") || !strcmp(id_terms.front(), "..")) {
        php_v8js_amd_split_terms((char *)base_module_id, terms);
        terms.pop_back();
    }

    terms.insert(terms.end(), id_terms.begin(), id_terms.end());

    std::vector<const char *> normalised_terms;

    for (std::vector<const char *>::iterator it = terms.begin(); it != terms.end(); it++) {
        const char *term = *it;

        if (!strcmp(term, "..")) {
            // Ignore parent term (..) if it's the first normalised term
            if (normalised_terms.size() > 0) {
                // Remove the parent normalized term
                normalised_terms.pop_back();
            }
        } else if (strcmp(term, ".")) {
            // Add the term if it's not the current term (.)
            normalised_terms.push_back(term);
        }
    }

    // Initialise the normalised module ID string
    *normalised_module_id = 0;

    for (std::vector<const char *>::iterator it = normalised_terms.begin(); it != normalised_terms.end(); it++) {
        if (strlen(normalised_module_id) > 0) {
            strcat(normalised_module_id, "/");
        }

        strcat(normalised_module_id, *it);
    }
}
