/*
 * Copyright (C) 2007-2013 Rich Waters <raw@goodhumans.net>
 *
 * This file is part of lib_mysqludf_preg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/**
 * @file lib_mysqludf_preg_check.c
 *
 * @brief Implements the PREG_CHECK mysql udf
 */


/**
 * @page PREG_CHECK PREG_CHECK
 *
 * @brief Test if a perl-compatible regular expression is valid
 *
 * @par Function Installation
 *    CREATE FUNCTION preg_check RETURNS INTEGER SONAME 'lib_mysqludf_preg.so';
 *
 * @par Synopsis
 *    PREG_CHECK( pattern )
 * 
 * @par
 *     @param pattern - is a string that might be a perl compatible regular 
 * expression as documented at:
 * http://us.php.net/manual/en/ref.pcre.php
 *
 *     @return 1 - the pcre is valid
 *     @return 0 - the pcre is NULL, empty, or a bad regex
 *
 * @details
 *    preg_check is a udf that tests if whether or not the given 
 * perl compatible regular expression is valid.  This is a useful
 * companion to the other functions in the lib_mysqludf_pref library
 * in that those functions all return errors (and stop processing) when
 * empty, NULL, or incorrect regular expressions are passed in to them.  
 * If pcre patterns are stored in rows of a database and it is
 * not confirmed that the patterns are valid, PCRE_CHECK is useful either
 * as a filter used in conjunction with those calls or as a separate 
 * query to help cleanup the database before using those other functions.
 *
 * @par Examples:
 *
 * SELECT PREG_CHECK('/The quick brown fox/i' );
 *
 * @b Yields:
 * @verbatim
   +---------------------------------------------------------------+
   | PREG_CHECK('/The quick brown fox/i' )                         |
   +---------------------------------------------------------------+
   |                                                             1 |
   +---------------------------------------------------------------+
@endverbatim
 *
 *  SELECT * from patterns WHERE PREG_CHECK( pattern );
 *      
 *  Yields:  all of the rows containing valid pcre's.
 */


#include "ghmysql.h"
#include "preg.h"



/**
 * Public function declarations:
 */
my_bool preg_check_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
longlong preg_check(UDF_INIT *initid __attribute__((unused)),
                      UDF_ARGS *args,
                      char *is_null __attribute__((unused)),
                      char *error __attribute__((unused)));
void preg_check_deinit( UDF_INIT* initid );


/*
 * Public function definitions:
 */

/**
 * @fn my_bool preg_check_init(UDF_INIT *initid, UDF_ARGS *args, 
 *                             char *message)
 *
 * @brief
 *     Perform the per-query initializations
 *
 * @param initid - various info supplied by mysql api - read mode at
 * http://dev.mysql.com/doc/refman/5.0/en/adding-udf.html
 *
 * @param args - array of information about arguments from the SQL call
 * See file documentation for the description of the SQL arguments
 *
 * @param message - for error messages.  Should be <80 but can be 255.
 *
 * @return 0 - on success
 * @return 1 - on error
 *
 * @details This function checks to make sure there is 1 argument.  It
 * then call pregInit to perform the common initializations.
 */
my_bool preg_check_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
    if (args->arg_count != 1)
    {
        strncpy(message,"preg_check: needs exactly one argument", MYSQL_ERRMSG_SIZE);
        return 1;
    }
    initid->maybe_null=0;	

    pregInit( initid , args , message );

    return 0;
}


/**
 * @fn longlong preg_check( UDF_INIT *initid ,  UDF_ARGS *args, char *is_null,
 *                          char *error )
 *
 * @brief
 *     The main routine for the PREG_CHECK udf.
 *
 * @param initid - various info supplied by mysql api - read more at
 * http://dev.mysql.com/doc/refman/5.0/en/adding-udf.html
 *
 * @param args - array of information about arguments from the SQL call
 * See file documentation for the description of the SQL arguments
 *
 * @param is_null - set this is return value is null
 * @param error - to be set if an error occurs
 *
 * @return 0 - if the given pattern is empty, NULL, or a bad regex.
 * @return 1 - if the given pattern is valid.
 *
 * @details This function calls pcre_ex from the pcre library to execute
 * the compiled pattern (which is either precompiled by ..._init
 * or compiled here for non-constant pattern arguments).  It then
 * does the appropriate thing with the returns :>)
 */
longlong preg_check( UDF_INIT *initid ,  UDF_ARGS *args, char *is_null,
                     char *error )
{
    char msg [ 255 ] ;
    struct preg_s *ptr ;
    pcre *re ;                  /* the compiled regex */


#ifndef GH_1_0_NULL_HANDLING
    if( ghargIsNullConstant( args , 0 ) )
    {
        *is_null = 1 ; 
        return NULL ; 
    }
#endif

    ptr = (struct preg_s *) initid->ptr ;
    if( args->args[0] && args->lengths[0] )
    {
        re = pregCompileRegexArg( args , msg , sizeof(msg)) ;
        if( !re )
        {
            return 0;
        }

        pcre_free( re ) ;
        return 1 ;
    }

    return 0 ;
}


/** 
 * @fn void preg_check_deinit(UDF_INIT *initid)
 *
 *      @brief cleanup after PREG_CHECK 
 *
 *      @param initid - pointer to struct to be cleaned.
 */
void preg_check_deinit(UDF_INIT *initid)
{
    pregDeInit( initid ) ;
}



