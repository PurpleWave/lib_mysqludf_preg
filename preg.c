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

/** @file preg.c
 *  
 * @brief Provides some functions that are shared by the various
 *        preg udf functions.   
 */

#include "ghmysql.h"
#include "preg.h"

/* For pthreads */
#include <pthread.h>

/*
 * Public Functions:
 */

/**
 * @fn pcre *pregCompileRegexArg( UDF_ARGS *args , char *msg , int msglen ) 
 *
 * @brief compile the regex (arg[0])
 *
 * @param args - the args supplied by mysql udf api (ultimately, the user)
 * @param msg - buffer where error messages can be placed
 * @param msglen - size of the error message buffer above
 * 
 * @return - if successful - the compiled regular expression
 * @return - if failure - NULL
 *
 * @details 
 *    This function compiles the pcre regular expression passed in as
 * the first argument.  The argument passed  
 * as args->args[0] is a pattern that needs to include delimiters and
 * may include modifiers.  (ie. /([a-z0-9]*?)(.*)/i ).  This function
 * is necessary because compileRegex (from_php.c) requires a string
 * argument.  This function null terminates the first argument and
 * calls compileRegex.
 *
 * @note 
 *    make sure to call pcre_free to free up the returned result (if not null)
 * 
 */
pcre *pregCompileRegexArg( UDF_ARGS *args , char *msg , int msglen ) 
{
    pcre *re ;                  /* the compiled pattern */
    char *val ;                 /* The pattern to compile */

    *msg ='\0';

    val = ghargdup( args , 0 ) ;
    if( !val )
    {
        if( args->lengths[0] && args->args[0] )
        {
            strncpy( msg , "Out of memory" , msglen ) ;
        }
        else
        {
            strncpy( msg , "Empty pattern" , msglen ) ;
        }
        return NULL ;
    }

    re = compileRegex( val , args->lengths[0], msg, msglen ) ;

    free( val ) ;

    return re ;
}


/**
 * @fn int initPtrInfo( struct preg_s *ptr ,UDF_ARGS *args,char *message )
 *
 * @brief initialize contents of initid->ptr 
 *
 * @param ptr - the pointer to initialize the info in
 * @param args - the args supplied by mysql udf api (ultimately, the user)
 * @param message - put error message in here if error
 * 
 * @return 0 - on success
 * @return 1 - on error
 *
 * @details 
 *   Compile the regex and save it in ptr->re.  This function should
 * normally only be called if the first argument is a constant.
 *
 * @note 
 *    make sure to call destroyPtrInfo when done
 */
int initPtrInfo( struct preg_s *ptr ,UDF_ARGS *args,char *message )
{
    // 128 is a safe size for mysql, which reccomends 80 chars or less messages
    ptr->re = pregCompileRegexArg( args, message,128 );
    if( !ptr->re )
    {
        return 1;
    }

    return 0 ;
}

/**
 * @fn int *pregCreateOffsetsVector( pcre *re , pcre_extra *extra , int *count, 
 *                                   char *msg , int msglen )
 *
 * @brief allocate a memory area that can be used for holding the offset vector
 * used by the pcre library.
 *
 * @param re - compiled regular expression
 * @param extra - NULL or extra info about re as returned by pcre_study
 * @param count - pointer to integer to place number of captures into
 * @param msg - put error messages here
 * @param msglen - length of msg buffer
 * 
 * @return pointer to new offets vector - on success
 * @return NULL if out of memory or error from pcre
 *
 * @details The ovector is used by the pcre for capturing the offsets of
 * the parenthesized sub-expressions of a pcre expression.  This function
 * uses pcre_fullinfo to retrieve the number of capture groups in the 
 * expression, and then it allocates a vector of that size+1 and
 * sets the passed in count to that size as well.
 */
int *pregCreateOffsetsVector( pcre *re , pcre_extra *extra , int *count ,
                              char *msg , int msglen )
{
    int *ovec ;                 /* vector to return */
    int oveccount ;             /* number of capture groups found */

    *count = 0 ;
    if( pcre_fullinfo(re, extra , PCRE_INFO_CAPTURECOUNT, &oveccount ) < 0 )
    {
        strncpy(msg,"preg: error retrieving information about pattern",msglen);
        return NULL ;
    }

    ++oveccount ;    // for 0
    oveccount *= 3 ; // 2 for offset info , 1 for pcre internals
    ovec  = malloc( sizeof( int ) * oveccount ) ;
    if( !ovec )
    {
        strncpy( msg , "preg: out of memory" , msglen ) ;
        return NULL ;
    }

    *count = oveccount ; 
    return ovec ;
}

/**
 * @fn int pregGetGroupNum( pcre *re ,  UDF_ARGS *args , int argnum )
 *
 * @brief gets the string number of a capture group from a pcre 
 *
 * @param re - compiled regular expression
 * @param args - the args to the mysql UDF 
 * @param argnum - the index of the args that specifies the group number to get
 * 
 * @return int >= 0 string number that can be passed to pcre_get_substring
 * @return -1 if group number not found or other error
 *
 * @details This function extract the desired group number from the 
 * given arguments.  If it is a named capture group, it is converted
 * to a number using pcre_get_stringnumber.  This number is then returned.
 */
int pregGetGroupNum( pcre *re ,  UDF_ARGS *args , int argnum )
{
    char *group ;               /* named group - args[argnum] */
    int groupnum ;              /* string number of capture group */
    
    // The groupnum was specified as an optional parameter
    if( argnum >= args->arg_count ) 
        groupnum = 0 ;
    else if( args->arg_type[argnum] == INT_RESULT )
    {   // numeric capture group
        groupnum = (int)(*(longlong *)args->args[2]) ;
    }
    else
    {
        // This is a named group. The numeric groupnum must be found
        group = ghargdup( args , 2 ) ;
        if( !group )  {
            fprintf(stderr,"pregGetGroupNum: error accessing capture group\n");
            return -1 ;
        }

        groupnum =pcre_get_stringnumber(re , group);
        free( group ) ;
    }

    return groupnum ; 
}

/**
 * @fn int pregSkipToOccurence( pcre *re , char *subject , int subject_len , 
 *                              int *ovector  , int oveccount , int occurence, 
 *                              int *rc)
 *
 * @brief return a pointer to the nth occurence of a pcre in a string
 *
 * @param re - compiled regular expression
 * @param subject - the string on which to perform matching
 * @param subject_len - length of the subject string
 * @param ovector - vector used by pcre to capture offets of matches
 * @param oveccount - size of ovector
 * @param occurence - match occurence to find
 * @param rc - put result of last pcre_exec call here
 * 
 * @return char * - portion of string which starts with pcre occurence requested
 * @return -1 if group number not found or other error
 *
 * @details This function extract the desired group number from the 
 * given arguments.  If it is a named capture group, it is converted
 * to a number using pcre_get_stringnumber.  This number is then returned.
 */
char *pregSkipToOccurence( pcre *re , char *subject , int subject_len , 
                              int *ovector  , int oveccount , int occurence, 
                              int *rc)
{
    char *ex_subject ;          /* position of last match */
    int subject_offset = 0 ;    /* offset of next match from last one */
    char *ret = NULL ;          /* return value from this function */
    pcre_extra extra;

    ex_subject = subject ; 
    
    memset(&extra, 0, sizeof(extra));
    pregSetLimits(&extra);
    
    // Skip over the 1st N occurences

    while( occurence-- && subject_offset <= subject_len ) {

        // Run the regex and find the groupnum if possible
        *rc = pcre_exec(re, &extra,  subject + subject_offset , 
                        subject_len - subject_offset, 0,0,
                        ovector, oveccount); 
        if( *rc <= 0 )
            break ;
        
        ex_subject = subject + subject_offset ; 
        subject_offset += ovector[1] ;
    }

    if( rc > 0 ) 
        ret = ex_subject ; 
    
    return ret ;
}

/**
 * @fn void destroyPtrInfo( struct preg_s *ptr )
 *
 * @brief free up the memory used by ptr and alloced in initPtrInfo
 *
 * @param ptr - free members of this struct
 */
void destroyPtrInfo( struct preg_s *ptr )
{
    if( ptr->re )
    {
        pcre_free( ptr->re ) ;
        ptr->re = NULL ;
    }
    if( ptr->return_buffer ) {
        free( ptr->return_buffer ) ;
        ptr->return_buffer = NULL ;
    }
}

/**
 * @fn void pregDeInit(UDF_INIT *initid)
 *
 * @brief shared _deinit routines used by preg.  
 *
 * @param initid - various info supplied by mysql api - read more at
 * http://dev.mysql.com/doc/refman/5.0/en/adding-udf.html
 *
 * @details - frees the ptr members and then frees the ptr itself.  It
 * can usually be the only thing called by the _deinit functions of the
 * preg routeines.
 */
void pregDeInit(UDF_INIT *initid)
{
    struct preg_s *ptr  ;       /* to avoid casting */

    if (initid->ptr)
    {
        ptr = (struct preg_s *)initid->ptr ;
        destroyPtrInfo( ptr ) ;
        free( ptr ) ;
        initid->ptr = NULL ;
    }
}


/**
 * @fn my_bool pregInit(UDF_INIT *initid, UDF_ARGS *args, char *message)
 *
 * @brief
 *     Perform the init stuff common to all preg routines
 *
 * @param initid - various info supplied by mysql api - read more at
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
 * @details This function is called from the _init routines for the preg 
 * functions.  It performs the initializations common to all or most of 
 * those routines.  This includes converting the 1st 2 args to strings,
 * and compiling  the first argument (the pattern) if it 
 * is a constant.
 */
my_bool pregInit(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
    struct preg_s *ptr;       /* temp holder of initid->ptr */
    int i ;

    // use calloc so deInit can check for NULL's before freeing
    initid->ptr = (char *)calloc( 1,sizeof( struct preg_s ) ) ;
    ptr = (struct preg_s *)initid->ptr ;

    if( !ptr )
    {
        strcpy(message,"not enough memory");
        return 1;
    }
    
    if( ghargIsNullConstant( args , 0 ) ) 
    {
        ptr->constant_pattern = 1 ;
#ifdef GH_1_0_NULL_HANDLING
        strcpy( message, "NULL pattern" ) ;
        return 1 ;
#endif
    }


    // Convert first 2 args (pattern & subject) to strings.
    for (i=0 ; i < 2; i++)
        args->arg_type[i]=STRING_RESULT;

    if(args->arg_count && args->args[0] ) 
    {
        if( initPtrInfo( ptr , args ,  message ) )
        {
            return 1;
        }

        /**
         * If the pattern is constant, compile it once to improve perfomance.
         * Set the constant_pattern member to inform main function.
         */
        ptr->constant_pattern = 1 ;
    }
    else
    {
        ptr->constant_pattern = 0 ;
    }

    if( ((int)initid->max_length) > 0 )
    {
        ptr->return_buffer_size = initid->max_length + 1 ;
    }
    else
    {
        // If there is no limit on max_length.  Start at a fairly big
        // size.   Re-allocations will occur if necessary.
        ptr->return_buffer_size = 1024000 ;
    }

    ptr->return_buffer = malloc( ptr->return_buffer_size ) ;

    return 0 ;
}

/**
 * int pregCopyToReturnBuffer( struct preg_s *ptr , char *s  , int l )
 *
 * @brief
 *     safely copies data into ptr->return_buffer
 *
 * @param ptr - the info stored in initid->ptr
 * @param s - data to be copied
 * @param l - length of data to be copied
 *
 * @return the number of bytes copied - on success
 * @return -1  - on error
 *
 * @details This function checks to see if ptr->return_buffer is big
 * enough to hold the given data.  If it isn't, reallocs occur.  
 * Then the data is copied.  
 *
 * @note
 *     The return buffer is null-terminated, as well.  This shouldn't be
 * necessary, but it can help to prevent potential crashes.
 */
int pregCopyToReturnBuffer( struct preg_s *ptr , char *s  , int l )
{
    char *newbuf ; 

    if( (l+1) > ptr->return_buffer_size )
    {
        newbuf = malloc( l + 1 ) ;
        if( !newbuf )
        {
            fprintf( stderr , 
                     "preg: out of memory reallocing return buffer\n" ) ;
            return -1 ;
        }

        free( ptr->return_buffer ) ;
        ptr->return_buffer = newbuf ; 
        ptr->return_buffer_size = l + 1 ;
    }

    memcpy( ptr->return_buffer , s , l ) ;
    ptr->return_buffer[ l ] = 0 ;

    return l ;
}

/**
 * @fn char *pregMoveToReturnValues( UDF_INIT *initid ,
 *                                   unsigned long *length , 
 *                                   char *is_null , char *error ,
 *                                   char *s , int s_len  ) 
 *
 * @brief
 *     set the appropriate UDF return values to the given data for UDF's
 * that return strings, and frees passed in data pointer.
 *
 * @param initid - various info supplied by mysql api - read more at
 * http://dev.mysql.com/doc/refman/5.0/en/adding-udf.html
 *
 * @param length - put the length of the returned string here.
 * @param is_null - set this if return value is null and NULL is allowed
 * @param error - set if an error occurs
 * @param s - string to copy into return buffer
 * @param s_len - length of string to copy into return buffer
 *
 * @return - pointer to the string to return (ptr->return_buffer)
 * @return - NULL - if error occured and NULL's are allowed
 *
 * @details This function checks the given length and data for 
 * negatives and NULL, respectively.  This function should usually
 * be called using the results from a pcre function call, and it 
 * prints the appropriate error message is the given length is <0.
 * Otherwise, it checks for non-NULL data uses pregCopyToReturnBuffer 
 * to copy given data into ptr->return_buffer.  If this copy is 
 * successful, it frees the passed in string.
 *
 * @note.  This function frees the passed in string after copying it.  Careful!
 */
char *pregMoveToReturnValues( UDF_INIT *initid ,
                              unsigned long *length , 
                              char *is_null , char *error ,
                              char *s , int s_len  ) 
{
    struct preg_s *ptr ;        /* local holder of initid->ptr */
    int l ;                     /* bytes copied into return_buffer */

    ptr = (struct preg_s *)initid->ptr ;

    // Set default return info.
    *error = 1 ;
    *length = 0 ;
    *ptr->return_buffer = '\0';
    if( initid->maybe_null )
    {
        *is_null = 1 ;
    }

    if( s_len >= 0 )
    {  
        if( s )
        {   // normal case -- copy data & free it
            l = pregCopyToReturnBuffer( ptr , s , s_len ) ;
            if( l >= 0 )
            {
                *is_null = 0 ;
                *error = 0 ;
                *length = l ;
            }
            free( s ) ;
        }
        else 
        {    // Empty string is not error?
            *is_null = 0 ;
            *error = 0 ;
        }
    }
    else
    {
        ghlogprintf("ERROR preg: pcre_exec rturned error %d (%s)\n" , s_len, pregExecErrorString(s_len) ) ;
    }

    if( *is_null )
    {
        return NULL ;
    }
    else
    {
        return ptr->return_buffer ;
    }
}

