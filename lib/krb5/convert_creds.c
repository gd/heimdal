/*
 * Copyright (c) 1997 - 2003 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "krb5_locl.h"
RCSID("$Id$");

#include "krb5-v4compat.h"

static krb5_error_code
check_ticket_flags(TicketFlags f)
{
    return 0; /* maybe add some more tests here? */
}

/* include this here, to avoid dependencies on libkrb */

static const int _tkt_lifetimes[TKTLIFENUMFIXED] = {
   38400,   41055,   43894,   46929,   50174,   53643,   57352,   61318,
   65558,   70091,   74937,   80119,   85658,   91581,   97914,  104684,
  111922,  119661,  127935,  136781,  146239,  156350,  167161,  178720,
  191077,  204289,  218415,  233517,  249664,  266926,  285383,  305116,
  326213,  348769,  372885,  398668,  426234,  455705,  487215,  520904,
  556921,  595430,  636601,  680618,  727680,  777995,  831789,  889303,
  950794, 1016537, 1086825, 1161973, 1242318, 1328218, 1420057, 1518247,
 1623226, 1735464, 1855462, 1983758, 2120925, 2267576, 2424367, 2592000
};

int
_krb5_krb_time_to_life(time_t start, time_t end)
{
    int i;
    time_t life = end - start;

    if (life > MAXTKTLIFETIME || life <= 0) 
	return 0;
#if 0    
    if (krb_no_long_lifetimes) 
	return (life + 5*60 - 1)/(5*60);
#endif
    
    if (end >= NEVERDATE)
	return TKTLIFENOEXPIRE;
    if (life < _tkt_lifetimes[0]) 
	return (life + 5*60 - 1)/(5*60);
    for (i=0; i<TKTLIFENUMFIXED; i++)
	if (life <= _tkt_lifetimes[i])
	    return i + TKTLIFEMINFIXED;
    return 0;
    
}

time_t
_krb5_krb_life_to_time(int start, int life_)
{
    unsigned char life = (unsigned char) life_;

#if 0    
    if (krb_no_long_lifetimes)
	return start + life*5*60;
#endif

    if (life == TKTLIFENOEXPIRE)
	return NEVERDATE;
    if (life < TKTLIFEMINFIXED)
	return start + life*5*60;
    if (life > TKTLIFEMAXFIXED)
	return start + MAXTKTLIFETIME;
    return start + _tkt_lifetimes[life - TKTLIFEMINFIXED];
}

/*
 * Get the name of the krb4 credentials cache, will use `tkfile' as
 * the name if that is passed in. `cc' must be free()ed by caller,
 */

static krb5_error_code
get_krb4_cc_name(const char *tkfile, char **cc)
{

    *cc = NULL;
    if(tkfile == NULL) {
	char *path;
	if(!issuid()) {
	    path = getenv("KRBTKFILE");
	    if (path)
		*cc = strdup(path);
	}
	if(*cc == NULL)
	    if (asprintf(cc, "%s%u", TKT_ROOT, (unsigned)getuid()) < 0)
		return errno;
    } else {
	*cc = strdup(tkfile);
	if (*cc == NULL)
	    return ENOMEM;
    }
    return 0;
}

/*
 * Write a Kerberos 4 ticket file
 */

#define KRB5_TF_LCK_RETRY_COUNT 50
#define KRB5_TF_LCK_RETRY 1

static krb5_error_code
write_v4_cc(krb5_context context, const char *tkfile, 
	    krb5_storage *sp, int append)
{
    krb5_error_code ret;
    struct stat sb;
    krb5_data data;
    char *path;
    int fd, i;

    ret = get_krb4_cc_name(tkfile, &path);
    if (ret) {
	krb5_set_error_string(context, 
			      "krb5_krb_tf_setup: failed getting "
			      "the krb4 credentials cache name"); 
	return ret;
    }

    fd = open(path, O_WRONLY|O_CREAT, 0600);
    if (fd < 0) {
	free(path);
	krb5_set_error_string(context, 
			      "krb5_krb_tf_setup: error opening file %s", 
			      path);
	return errno;
    }

    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
	free(path);
	close(fd);
	krb5_set_error_string(context, 
			      "krb5_krb_tf_setup: tktfile %s is not a file",
			      path);
	return KRB5_FCC_PERM;
    }

    for (i = 0; i < KRB5_TF_LCK_RETRY_COUNT; i++) {
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
	    sleep(KRB5_TF_LCK_RETRY);
	} else
	    break;
    }
    if (i == KRB5_TF_LCK_RETRY_COUNT) {
	free(path);
	close(fd);
	krb5_set_error_string(context,
			      "krb5_krb_tf_setup: failed to lock %s",
			      path);
	return KRB5_FCC_PERM;
    }

    if (!append) {
	ret = ftruncate(fd, 0);
	if (ret < 0) {
	    flock(fd, LOCK_UN);
	    free(path);
	    close(fd);
	    krb5_set_error_string(context,
				  "krb5_krb_tf_setup: failed to truncate %s",
				  path);
	    return KRB5_FCC_PERM;
	}
    }
    ret = lseek(fd, 0L, SEEK_END);
    if (ret < 0) {
	ret = errno;
	flock(fd, LOCK_UN);
	free(path);
	close(fd);
	return ret;
    }

    krb5_storage_to_data(sp, &data);

    ret = write(fd, data.data, data.length);
    if (ret != data.length)
	ret = KRB5_CC_IO;

    krb5_free_data_contents(context, &data);

    flock(fd, LOCK_UN);
    free(path);
    close(fd);

    return 0;
}


krb5_error_code
_krb5_krb_tf_setup(krb5_context context, 
		   struct credentials *v4creds, 
		   const char *tkfile,
		   int append)
{
    krb5_error_code ret;
    krb5_storage *sp;

    sp = krb5_storage_emem();
    if (sp == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(sp, KRB5_STORAGE_BYTEORDER_HOST);
    krb5_storage_set_eof_code(sp, KRB5_CC_IO);

    krb5_clear_error_string(context);

    if (!append) {
	ret = krb5_store_stringz(sp, v4creds->pname);
	if (ret < 0)
	    goto error;
	ret = krb5_store_stringz(sp, v4creds->pinst);
	if (ret < 0)
	    goto error;
    }

    /* cred */
    ret = krb5_store_stringz(sp, v4creds->service);
    if (ret < 0)
	goto error;
    ret = krb5_store_stringz(sp, v4creds->instance);
    if (ret < 0)
	goto error;
    ret = krb5_store_stringz(sp, v4creds->realm);
    if (ret < 0)
	goto error;
    ret = krb5_storage_write(sp, v4creds->session, 8);
    if (ret != 8) {
	ret = KRB5_CC_IO;
	goto error;
    }
    ret = krb5_store_int32(sp, v4creds->lifetime);
    if (ret)
	goto error;
    ret = krb5_store_int32(sp, v4creds->kvno);
    if (ret)
	goto error;
    ret = krb5_store_int32(sp, v4creds->ticket_st.length);
    if (ret)
	goto error;

    ret = krb5_storage_write(sp, v4creds->ticket_st.dat, 
			     v4creds->ticket_st.length);
    if (ret != v4creds->ticket_st.length) {
	ret = KRB5_CC_IO;
	goto error;
    }
    ret = krb5_store_int32(sp, v4creds->issue_date);
    if (ret)
	goto error;

    ret = write_v4_cc(context, tkfile, sp, append);

 error:
    krb5_storage_free(sp);

    return ret;
}

krb5_error_code
_krb5_krb_dest_tkt(krb5_context context, const char *tkfile)
{
    krb5_error_code ret;
    char *path;

    ret = get_krb4_cc_name(tkfile, &path);
    if (ret) {
	krb5_set_error_string(context, 
			      "krb5_krb_tf_setup: failed getting "
			      "the krb4 credentials cache name"); 
	return ret;
    }

    if (unlink(path) < 0) {
	ret = errno;
	krb5_set_error_string(context, 
			      "krb5_krb_dest_tkt failed removing the cache "
			      "with error %s", strerror(ret));
    }
    free(path);

    return ret;
}


/* Convert the v5 credentials in `in_cred' to v4-dito in `v4creds'.
 * This is done by sending them to the 524 function in the KDC.  If
 * `in_cred' doesn't contain a DES session key, then a new one is
 * gotten from the KDC and stored in the cred cache `ccache'.
 */

krb5_error_code
krb524_convert_creds_kdc(krb5_context context, 
			 krb5_creds *in_cred,
			 struct credentials *v4creds)
{
    krb5_error_code ret;
    krb5_data reply;
    krb5_storage *sp;
    int32_t tmp;
    krb5_data ticket;
    char realm[REALM_SZ];
    krb5_creds *v5_creds = in_cred;

    ret = check_ticket_flags(v5_creds->flags.b);
    if(ret)
	goto out2;

    {
	krb5_krbhst_handle handle;

	ret = krb5_krbhst_init(context,
			       krb5_principal_get_realm(context, 
							v5_creds->server),
			       KRB5_KRBHST_KRB524,
			       &handle);
	if (ret)
	    goto out2;

	ret = krb5_sendto (context,
			   &v5_creds->ticket,
			   handle,
			   &reply);
	krb5_krbhst_free(context, handle);
	if (ret)
	    goto out2;
    }
    sp = krb5_storage_from_mem(reply.data, reply.length);
    if(sp == NULL) {
	ret = ENOMEM;
	krb5_set_error_string (context, "malloc: out of memory");
	goto out2;
    }
    krb5_ret_int32(sp, &tmp);
    ret = tmp;
    if(ret == 0) {
	memset(v4creds, 0, sizeof(*v4creds));
	ret = krb5_ret_int32(sp, &tmp);
	if(ret)
	    goto out;
	v4creds->kvno = tmp;
	ret = krb5_ret_data(sp, &ticket);
	if(ret)
	    goto out;
	v4creds->ticket_st.length = ticket.length;
	memcpy(v4creds->ticket_st.dat, ticket.data, ticket.length);
	krb5_data_free(&ticket);
	ret = krb5_524_conv_principal(context, 
				      v5_creds->server, 
				      v4creds->service, 
				      v4creds->instance, 
				      v4creds->realm);
	if(ret)
	    goto out;
	v4creds->issue_date = v5_creds->times.starttime;
	v4creds->lifetime = _krb5_krb_time_to_life(v4creds->issue_date,
						   v5_creds->times.endtime);
	ret = krb5_524_conv_principal(context, v5_creds->client, 
				      v4creds->pname, 
				      v4creds->pinst, 
				      realm);
	if(ret)
	    goto out;
	memcpy(v4creds->session, v5_creds->session.keyvalue.data, 8);
    } else {
	krb5_set_error_string(context, "converting credentials: %s", 
			      krb5_get_err_text(context, ret));
    }
out:
    krb5_storage_free(sp);
    krb5_data_free(&reply);
out2:
    if (v5_creds != in_cred)
	krb5_free_creds (context, v5_creds);
    return ret;
}

krb5_error_code
krb524_convert_creds_kdc_ccache(krb5_context context, 
				krb5_ccache ccache,
				krb5_creds *in_cred,
				struct credentials *v4creds)
{
    krb5_error_code ret;
    krb5_creds *v5_creds = in_cred;
    krb5_keytype keytype;

    keytype = v5_creds->session.keytype;

    if (keytype != ENCTYPE_DES_CBC_CRC) {
	/* MIT krb524d doesn't like nothing but des-cbc-crc tickets,
           so go get one */
	krb5_creds template;

	memset (&template, 0, sizeof(template));
	template.session.keytype = ENCTYPE_DES_CBC_CRC;
	ret = krb5_copy_principal (context, in_cred->client, &template.client);
	if (ret) {
	    krb5_free_creds_contents (context, &template);
	    return ret;
	}
	ret = krb5_copy_principal (context, in_cred->server, &template.server);
	if (ret) {
	    krb5_free_creds_contents (context, &template);
	    return ret;
	}

	ret = krb5_get_credentials (context, 0, ccache,
				    &template, &v5_creds);
	krb5_free_creds_contents (context, &template);
	if (ret)
	    return ret;
    }

    ret = krb524_convert_creds_kdc(context, v5_creds, v4creds);

    if (v5_creds != in_cred)
	krb5_free_creds (context, v5_creds);
    return ret;
}
