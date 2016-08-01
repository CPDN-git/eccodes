/*
 * Copyright 2005-2016 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 *
 * In applying this licence, ECMWF does not waive the privileges and immunities granted to it by
 * virtue of its status as an intergovernmental organisation nor does it submit to any jurisdiction.
 */

#include "grib_api_internal.h"
#include <ctype.h>
/*
   This is used by make_class.pl

   START_CLASS_DEF
   CLASS      = dumper
   IMPLEMENTS = dump_long;dump_bits
   IMPLEMENTS = dump_double;dump_string;dump_string_array
   IMPLEMENTS = dump_bytes;dump_values
   IMPLEMENTS = dump_label;dump_section
   IMPLEMENTS = init;destroy
   IMPLEMENTS = header;footer
   MEMBERS = long section_offset
   MEMBERS = long empty
   MEMBERS = long end
   MEMBERS = long isLeaf
   MEMBERS = long isAttribute
   MEMBERS = grib_string_list* keys
   END_CLASS_DEF

 */


/* START_CLASS_IMP */

/*

Don't edit anything between START_CLASS_IMP and END_CLASS_IMP
Instead edit values between START_CLASS_DEF and END_CLASS_DEF
or edit "dumper.class" and rerun ./make_class.pl

*/

static void init_class      (grib_dumper_class*);
static int init            (grib_dumper* d);
static int destroy         (grib_dumper*);
static void dump_long       (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_bits       (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_double     (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_string     (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_string_array     (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_bytes      (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_values     (grib_dumper* d, grib_accessor* a);
static void dump_label      (grib_dumper* d, grib_accessor* a,const char* comment);
static void dump_section    (grib_dumper* d, grib_accessor* a,grib_block_of_accessors* block);
static void header         (grib_dumper*,grib_handle*);
static void footer         (grib_dumper*,grib_handle*);

typedef struct grib_dumper_python {
    grib_dumper          dumper;  
/* Members defined in fortran */
	long section_offset;
	long empty;
	long end;
	long isLeaf;
	long isAttribute;
	grib_string_list* keys;
} grib_dumper_python;


static grib_dumper_class _grib_dumper_class_python = {
    0,                              /* super                     */
    "fortran",                              /* name                      */
    sizeof(grib_dumper_python),     /* size                      */
    0,                                   /* inited */
    &init_class,                         /* init_class */
    &init,                               /* init                      */
    &destroy,                            /* free mem                       */
    &dump_long,                          /* dump long         */
    &dump_double,                        /* dump double    */
    &dump_string,                        /* dump string    */
    &dump_string_array,                        /* dump string array   */
    &dump_label,                         /* dump labels  */
    &dump_bytes,                         /* dump bytes  */
    &dump_bits,                          /* dump bits   */
    &dump_section,                       /* dump section      */
    &dump_values,                        /* dump values   */
    &header,                             /* header   */
    &footer,                             /* footer   */
};

grib_dumper_class* grib_dumper_class_python = &_grib_dumper_class_python;

/* END_CLASS_IMP */
static void dump_attributes(grib_dumper* d,grib_accessor* a,char* prefix);

GRIB_INLINE static int grib_inline_strcmp(const char* a,const char* b)
{
    if (*a != *b) return 1;
    while((*a!=0 && *b!=0) &&  *(a) == *(b) ) {a++;b++;}
    return (*a==0 && *b==0) ? 0 : 1;
}

typedef struct string_count string_count;
struct string_count {
    char* value;
    int count;
    string_count* next;
};

static int get_key_rank(grib_handle* h,grib_string_list* keys,const char* key)
{
    grib_string_list* next=keys;
    grib_string_list* prev=keys;
    int ret=0;
    size_t size=0;
    grib_context* c=h->context;

    while (next && next->value && grib_inline_strcmp(next->value,key)) {
        prev=next;
        next=next->next;
    }
    if (!next) {
        prev->next=(grib_string_list*)grib_context_malloc_clear(c,sizeof(grib_string_list));
        next=prev->next;
    }
    if (!next->value) {
        next->value=strdup(key);
        next->count=0;
    }

    next->count++;
    ret=next->count;
    if (ret==1) {
        char* s=grib_context_malloc_clear(c,strlen(key)+5);
        sprintf(s,"#2#%s",key);
        if (grib_get_size(h,s,&size)==GRIB_NOT_FOUND) ret=0;
    }

    return ret;
}

static int depth=0;

static void init_class      (grib_dumper_class* c){}

static int init(grib_dumper* d)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    grib_context* c=d->handle->context;
    self->section_offset=0;
    self->empty=1;
    d->count=1;
    self->isLeaf=0;
    self->isAttribute=0;
    self->keys=grib_context_malloc_clear(c,sizeof(grib_string_list));

    return GRIB_SUCCESS;
}

static int destroy(grib_dumper* d)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    grib_string_list* next=self->keys;
    grib_string_list* cur=self->keys;
    grib_context* c=d->handle->context;
    while(next) {
        cur=next;
        next=next->next;
        grib_context_free(c,cur->value);
        grib_context_free(c,cur);
    }
    return GRIB_SUCCESS;
}

static char* dval_to_string(grib_context* c,double v)
{
    char* sval=grib_context_malloc_clear(c,sizeof(char)*40);
    char* p;
    sprintf(sval,"%.18e",v);
    p=sval;
    while (*p !=0 ) {
        if (*p == 'e') *p='d';
        p++;
    }
    return sval;
}

static void dump_values(grib_dumper* d,grib_accessor* a)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    double value; size_t size = 1;
    double *values=NULL;
    int err = 0;
    int i,r,icount;
    int cols=2;
    long count=0;
    char* sval;
    grib_context* c=a->context;
    grib_handle* h=grib_handle_of_accessor(a);

    grib_value_count(a,&count);
    size=count;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0 || (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) !=0)
        return;

    if (size>1) {
        values=(double*)grib_context_malloc_clear(c,sizeof(double)*size);
        err=grib_unpack_double(a,values,&size);
    } else {
        err=grib_unpack_double(a,&value,&size);
    }

    self->empty=0;

    if (size>1) {

        fprintf(self->dumper.out,"  if(allocated(rvalues)) deallocate(rvalues)\n");
        fprintf(self->dumper.out,"  allocate(rvalues(%ld))\n",size);


        fprintf(self->dumper.out,"  rvalues=(/");

        icount=0;
        for (i=0; i<size-1; ++i) {
            if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
            sval=dval_to_string(c,values[i]);
            fprintf(self->dumper.out,"%s, ", sval);
            grib_context_free(c,sval);
            icount++;
        }
        if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
        sval=dval_to_string(c,values[i]);
        fprintf(self->dumper.out,"%s", sval);
        grib_context_free(c,sval);

        depth-=2;
        fprintf(self->dumper.out,"/)\n");
        grib_context_free(c,values);

        if ((r=get_key_rank(h,self->keys,a->name))!=0)
            fprintf(self->dumper.out,"  call codes_set(ibufr,'#%d#%s',rvalues)\n",r,a->name);
        else
            fprintf(self->dumper.out,"  call codes_set(ibufr,'%s',rvalues)\n",a->name);
    } else {
        r=get_key_rank(h,self->keys,a->name);
        if( !grib_is_missing_double(a,value) ) {

            sval=dval_to_string(c,value);
            if (r!=0)
                fprintf(self->dumper.out,"  call codes_set(ibufr,'#%d#%s',%s)\n",r,a->name,sval);
            else
                fprintf(self->dumper.out,"  call codes_set(ibufr,'%s',%s)\n",a->name,sval);

            grib_context_free(c,sval);

        }
    }

    if (self->isLeaf==0) {
        char* prefix;
        int dofree=0;

        if (r!=0) {
            prefix=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+10));
            dofree=1;
            sprintf(prefix,"#%d#%s",r,a->name);
        } else prefix=(char*)a->name;

        dump_attributes(d,a,prefix);
        if (dofree) grib_context_free(c,prefix);
        depth-=2;
    }

    (void)err; /* TODO */
}

static void dump_values_attribute(grib_dumper* d,grib_accessor* a,char* prefix)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    double value; size_t size = 1;
    double *values=NULL;
    int err = 0;
    int i,icount;
    int cols=2;
    long count=0;
    char* sval;
    grib_context* c=a->context;

    grib_value_count(a,&count);
    size=count;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0 || (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) !=0)
        return;

    if (size>1) {
        values=(double*)grib_context_malloc_clear(c,sizeof(double)*size);
        err=grib_unpack_double(a,values,&size);
    } else {
        err=grib_unpack_double(a,&value,&size);
    }

    self->empty=0;

    if (size>1) {

        fprintf(self->dumper.out,"  if(allocated(rvalues)) deallocate(rvalues)\n");
        fprintf(self->dumper.out,"  allocate(rvalues(%ld))\n",size);


        fprintf(self->dumper.out,"  rvalues=(/");

        icount=0;
        for (i=0; i<size-1; ++i) {
            if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
            sval=dval_to_string(c,values[i]);
            fprintf(self->dumper.out,"%s, ", sval);
            grib_context_free(c,sval);
            icount++;
        }
        if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
        sval=dval_to_string(c,values[i]);
        fprintf(self->dumper.out,"%s", sval);
        grib_context_free(c,sval);

        depth-=2;
        fprintf(self->dumper.out,"/)\n");
        grib_context_free(c,values);

        fprintf(self->dumper.out,"  call codes_set(ibufr,'%s->%s' &\n,rvalues)\n",prefix,a->name);
    } else {
        /* int r=get_key_rank(h,self->keys,a->name); */
        if( !grib_is_missing_double(a,value) ) {

            sval=dval_to_string(c,value);
            fprintf(self->dumper.out,"  call codes_set(ibufr,'%s->%s' &\n,%s)\n",prefix,a->name,sval);

            grib_context_free(c,sval);

        }
    }

    if (self->isLeaf==0) {
        char* prefix1;

        prefix1=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+strlen(prefix)+5));
        sprintf(prefix1,"%s->%s",prefix,a->name);

        dump_attributes(d,a,prefix1);

        grib_context_free(c,prefix1);
        depth-=2;
    }

    (void)err; /* TODO */
}

static void dump_long(grib_dumper* d,grib_accessor* a,const char* comment)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    long value; size_t size = 1;
    long *values=NULL;
    int err = 0;
    int i,r,icount;
    int cols=4;
    long count=0;
    grib_context* c=a->context;
    grib_handle* h=grib_handle_of_accessor(a);

    grib_value_count(a,&count);
    size=count;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0  ) return;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) != 0) {
        if (self->isLeaf==0) {
            char* prefix;
            int dofree=0;

            r=get_key_rank(h,self->keys,a->name);
            if (r!=0) {
                prefix=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+10));
                dofree=1;
                sprintf(prefix,"#%d#%s",r,a->name);
            } else prefix=(char*)a->name;

            dump_attributes(d,a,prefix);
            if (dofree) grib_context_free(c,prefix);
            depth-=2;
        }
        return;
    }

    if (size>1) {
        values=(long*)grib_context_malloc_clear(a->context,sizeof(long)*size);
        err=grib_unpack_long(a,values,&size);
    } else {
        err=grib_unpack_long(a,&value,&size);
    }

    self->empty=0;

    if (size>1) {
        fprintf(self->dumper.out,"  if(allocated(ivalues)) deallocate(ivalues)\n");
        fprintf(self->dumper.out,"  allocate(ivalues(%ld))\n",size);


        fprintf(self->dumper.out,"  ivalues=(/");
        icount=0;
        for (i=0;i<size-1;i++) {
            if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
            fprintf(self->dumper.out,"%ld, ",values[i]);
            icount++;
        }
        if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
        fprintf(self->dumper.out,"%ld ",values[i]);

        depth-=2;
        fprintf(self->dumper.out,"/)\n");
        grib_context_free(a->context,values);

        if ((r=get_key_rank(h,self->keys,a->name))!=0)
            fprintf(self->dumper.out,"  call codes_set(ibufr,'#%d#%s',ivalues)\n",r,a->name);
        else
            fprintf(self->dumper.out,"  call codes_set(ibufr,'%s',ivalues)\n",a->name);

    } else {
        r=get_key_rank(h,self->keys,a->name);
        if( !grib_is_missing_long(a,value) ) {
            if (r!=0)
                fprintf(self->dumper.out,"  call codes_set(ibufr,'#%d#%s',",r,a->name);
            else
                fprintf(self->dumper.out,"  call codes_set(ibufr,'%s',",a->name);

            fprintf(self->dumper.out,"%ld)\n",value);
        }
    }

    if (self->isLeaf==0) {
        char* prefix;
        int dofree=0;

        if (r!=0) {
            prefix=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+10));
            dofree=1;
            sprintf(prefix,"#%d#%s",r,a->name);
        } else prefix=(char*)a->name;

        dump_attributes(d,a,prefix);
        if (dofree) grib_context_free(c,prefix);
        depth-=2;
    }
    (void)err; /* TODO */
}

static void dump_long_attribute(grib_dumper* d,grib_accessor* a,char* prefix)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    long value; size_t size = 1;
    long *values=NULL;
    int err = 0;
    int i,icount;
    int cols=4;
    long count=0;
    grib_context* c=a->context;

    grib_value_count(a,&count);
    size=count;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0 || (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) != 0)
        return;

    if (size>1) {
        values=(long*)grib_context_malloc_clear(a->context,sizeof(long)*size);
        err=grib_unpack_long(a,values,&size);
    } else {
        err=grib_unpack_long(a,&value,&size);
    }

    self->empty=0;

    if (size>1) {
        fprintf(self->dumper.out,"  if(allocated(ivalues)) deallocate(ivalues)\n");
        fprintf(self->dumper.out,"  allocate(ivalues(%ld))\n",size);


        fprintf(self->dumper.out,"  ivalues=(/");
        icount=0;
        for (i=0;i<size-1;i++) {
            if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
            fprintf(self->dumper.out,"%ld, ",values[i]);
            icount++;
        }
        if (icount>cols || i==0) {fprintf(self->dumper.out,"  &\n      ");icount=0;}
        fprintf(self->dumper.out,"%ld ",values[i]);

        depth-=2;
        fprintf(self->dumper.out,"/)\n");
        grib_context_free(a->context,values);

        fprintf(self->dumper.out,"  call codes_set(ibufr,'%s->%s' &\n,ivalues)\n",prefix,a->name);

    } else {
        /* int r=get_key_rank(h,self->keys,a->name); */
        if( !grib_is_missing_long(a,value) ) {
            fprintf(self->dumper.out,"  call codes_set(ibufr,'%s->%s'&\n,",prefix,a->name);
            fprintf(self->dumper.out,"%ld)\n",value);
        }
    }

    if (self->isLeaf==0) {
        char* prefix1;

        prefix1=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+strlen(prefix)+5));
        sprintf(prefix1,"%s->%s",prefix,a->name);

        dump_attributes(d,a,prefix1);

        grib_context_free(c,prefix1);
        depth-=2;
    }
    (void)err; /* TODO */
}

static void dump_bits(grib_dumper* d,grib_accessor* a,const char* comment)
{
}

static void dump_double(grib_dumper* d,grib_accessor* a,const char* comment)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    double value; size_t size = 1;
    int r;
    char* sval;
    grib_handle* h=grib_handle_of_accessor(a);
    grib_context* c=h->context;

    grib_unpack_double(a,&value,&size);
    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0 || (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) != 0)
        return;

    self->empty=0;

    r=get_key_rank(h,self->keys,a->name);
    if( !grib_is_missing_double(a,value) ) {
        sval=dval_to_string(c,value);
        if (r!=0)
            fprintf(self->dumper.out,"  call codes_set(ibufr,'#%d#%s',%s)\n",r,a->name,sval);
        else
            fprintf(self->dumper.out,"  call codes_set(ibufr,'%s',%s)\n",a->name,sval);

        grib_context_free(c,sval);
    }

    if (self->isLeaf==0) {
        char* prefix;
        int dofree=0;

        if (r!=0) {
            prefix=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+10));
            dofree=1;
            sprintf(prefix,"#%d#%s",r,a->name);
        } else prefix=(char*)a->name;

        dump_attributes(d,a,prefix);
        if (dofree) grib_context_free(c,prefix);
        depth-=2;
    }
}

static void dump_string_array(grib_dumper* d,grib_accessor* a,const char* comment)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    char **values;
    size_t size = 0,i=0;
    grib_context* c=NULL;
    int err = 0;
    long count=0;
    int r;
    grib_handle* h=grib_handle_of_accessor(a);

    c=a->context;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0 || (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) != 0)
        return;

    grib_value_count(a,&count);
    size=count;
    if (size==1) {
        dump_string(d,a,comment);
        return;
    }

    fprintf(self->dumper.out,"  if(allocated(svalues)) deallocate(svalues)\n");
    fprintf(self->dumper.out,"  allocate(svalues(%ld))\n",size);

    fprintf(self->dumper.out,"  svalues=(/");


    self->empty=0;

    values=(char**)grib_context_malloc_clear(c,size*sizeof(char*));
    if (!values) {
        grib_context_log(c,GRIB_LOG_FATAL,"unable to allocate %d bytes",(int)size);
        return;
    }

    err = grib_unpack_string_array(a,values,&size);

    for  (i=0;i<size-1;i++) {
        fprintf(self->dumper.out,"    \"%s\", &\n",values[i]);
    }
    fprintf(self->dumper.out,"    \"%s\" /)\n",values[i]);


    if (self->isLeaf==0) {
        if ((r=get_key_rank(h,self->keys,a->name))!=0)
            fprintf(self->dumper.out,"  call codes_set_string_array(ibufr,'#%d#%s',svalues)\n",r,a->name);
        else
            fprintf(self->dumper.out,"  call codes_set_string_array(ibufr,'%s',svalues)\n",a->name);
    }

    if (self->isLeaf==0) {
        char* prefix;
        int dofree=0;

        if (r!=0) {
            prefix=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+10));
            dofree=1;
            sprintf(prefix,"#%d#%s",r,a->name);
        } else prefix=(char*)a->name;

        dump_attributes(d,a,prefix);
        if (dofree) grib_context_free(c,prefix);
        depth-=2;
    }

    grib_context_free(c,values);
    (void)err; /* TODO */
}

static void dump_string(grib_dumper* d,grib_accessor* a,const char* comment)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    char *value=NULL;
    char *p = NULL;
    size_t size = 0;
    grib_context* c=NULL;
    int r;
    int err = _grib_get_string_length(a,&size);
    grib_handle* h=grib_handle_of_accessor(a);

    c=a->context;
    if (size==0) return;

    if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0 || (a->flags & GRIB_ACCESSOR_FLAG_READ_ONLY) != 0)
        return;

    value=(char*)grib_context_malloc_clear(c,size);
    if (!value) {
        grib_context_log(c,GRIB_LOG_FATAL,"unable to allocate %d bytes",(int)size);
        return;
    }

    self->empty=0;

    err = grib_unpack_string(a,value,&size);
    p=value;
    r=get_key_rank(h,self->keys,a->name);
    if (grib_is_missing_string(a,(unsigned char *)value,size))
        return;

    while(*p) { if(!isprint(*p)) *p = '.'; p++; }

    if (self->isLeaf==0) {
        depth+=2;
        if (r!=0)
            fprintf(self->dumper.out,"  call codes_set(ibufr,'#%d#%s',",r,a->name);
        else
            fprintf(self->dumper.out,"  call codes_set(ibufr,'%s',",a->name);
    }
    fprintf(self->dumper.out,"\'%s\')\n",value);


    if (self->isLeaf==0) {
        char* prefix;
        int dofree=0;

        if (r!=0) {
            prefix=grib_context_malloc_clear(c,sizeof(char)*(strlen(a->name)+10));
            dofree=1;
            sprintf(prefix,"#%d#%s",r,a->name);
        } else prefix=(char*)a->name;

        dump_attributes(d,a,prefix);
        if (dofree) grib_context_free(c,prefix);
        depth-=2;
    }

    grib_context_free(c,value);
    (void)err; /* TODO */
}

static void dump_bytes(grib_dumper* d,grib_accessor* a,const char* comment)
{
}

static void dump_label(grib_dumper* d,grib_accessor* a,const char* comment)
{
}

static void _dump_long_array(grib_handle* h,FILE* f,const char* key,const char* print_key)
{
    long* val;
    size_t size=0,i;
    int cols=9,icount=0;


    if (grib_get_size(h,key,&size)==GRIB_NOT_FOUND) return;

    fprintf(f,"  if(allocated(ivalues)) deallocate(ivalues)\n");
    fprintf(f,"  allocate(ivalues(%ld))\n",size);

    fprintf(f,"  ivalues=(/ ");

    val=grib_context_malloc_clear(h->context,sizeof(long)*size);
    grib_get_long_array(h,key,val,&size);
    for (i=0;i<size-1;i++) {
        if (icount>cols || i==0) {fprintf(f,"  &\n      ");icount=0;}
        fprintf(f,"%ld, ",val[i]);
        icount++;
    }
    if (icount>cols) {fprintf(f,"  &\n      ");}
    fprintf(f,"%ld /)\n",val[size-1]);

    grib_context_free(h->context,val);
    fprintf(f,"  call codes_set(ibufr,'%s',ivalues)\n",print_key);

}

static void dump_section(grib_dumper* d,grib_accessor* a,grib_block_of_accessors* block)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    if (!grib_inline_strcmp(a->name,"BUFR") ||
            !grib_inline_strcmp(a->name,"GRIB") ||
            !grib_inline_strcmp(a->name,"META")
    ) {
        grib_handle* h=grib_handle_of_accessor(a);
        depth=2;
        self->empty=1;
        depth+=2;
        _dump_long_array(h,self->dumper.out,"dataPresentIndicator","inputDataPresentIndicator");
        _dump_long_array(h,self->dumper.out,"delayedDescriptorReplicationFactor","inputDelayedDescriptorReplicationFactor");
        _dump_long_array(h,self->dumper.out,"shortDelayedDescriptorReplicationFactor","inputShortDelayedDescriptorReplicationFactor");
        _dump_long_array(h,self->dumper.out,"extendedDelayedDescriptorReplicationFactor","inputExtendedDelayedDescriptorReplicationFactor");
        grib_dump_accessors_block(d,block);
        depth-=2;
    } else if (!grib_inline_strcmp(a->name,"groupNumber")) {
        if ( (a->flags & GRIB_ACCESSOR_FLAG_DUMP) == 0)
            return;
        self->empty=1;
        depth+=2;
        grib_dump_accessors_block(d,block);
        depth-=2;
    } else {
        grib_dump_accessors_block(d,block);
    }
}

static void dump_attributes(grib_dumper* d,grib_accessor* a,char* prefix)
{
    int i=0;
    grib_dumper_python *self = (grib_dumper_python*)d;
    unsigned long flags;
    while (i < MAX_ACCESSOR_ATTRIBUTES && a->attributes[i]) {
        self->isAttribute=1;
        if (  (d->option_flags & GRIB_DUMP_FLAG_ALL_ATTRIBUTES ) == 0
                && (a->attributes[i]->flags & GRIB_ACCESSOR_FLAG_DUMP)== 0 )
        {
            i++;
            continue;
        }
        self->isLeaf=a->attributes[i]->attributes[0]==NULL ? 1 : 0;
        flags=a->attributes[i]->flags;
        a->attributes[i]->flags |= GRIB_ACCESSOR_FLAG_DUMP;
        switch (grib_accessor_get_native_type(a->attributes[i])) {
        case GRIB_TYPE_LONG:
            dump_long_attribute(d,a->attributes[i],prefix);
            break;
        case GRIB_TYPE_DOUBLE:
            dump_values_attribute(d,a->attributes[i],prefix);
            break;
        case GRIB_TYPE_STRING:
            break;
        }
        a->attributes[i]->flags=flags;
        i++;
    }
    self->isLeaf=0;
    self->isAttribute=0;
}

static void header(grib_dumper* d,grib_handle* h)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    char sampleName[200]={0};
    long localSectionPresent,edition,bufrHeaderCentre,isSatellite;

    grib_get_long(h,"localSectionPresent",&localSectionPresent);
    grib_get_long(h,"bufrHeaderCentre",&bufrHeaderCentre);
    grib_get_long(h,"edition",&edition);

    if (localSectionPresent && bufrHeaderCentre==98 ) {
        grib_get_long(h,"isSatellite",&isSatellite);
        if (isSatellite)
            sprintf(sampleName,"BUFR%ld_local_satellite.bufr",edition);
        else
            sprintf(sampleName,"BUFR%ld_local.bufr",edition);
    } else {
        sprintf(sampleName,"BUFR%ld.bufr",edition);
    }

    if (d->count<2) {
        /* This is the first message being processed */
        fprintf(self->dumper.out, "#  This program has been automatically generated with bufr_dump -Epython\n");
        fprintf(self->dumper.out, "#  Using ecCodes version: ");
        grib_print_api_version(self->dumper.out);
        fprintf(self->dumper.out, "\n\n");
        fprintf(self->dumper.out,"import traceback\n");
        fprintf(self->dumper.out,"import sys\n");
        fprintf(self->dumper.out,"from eccodes import *\n\n");
        fprintf(self->dumper.out,"def bufr_create_message():\n");
    }
    fprintf(self->dumper.out,"    ibufr = codes_new_from_samples('%s')\n",sampleName);
}

static void footer(grib_dumper* d,grib_handle* h)
{
    grib_dumper_python *self = (grib_dumper_python*)d;
    fprintf(self->dumper.out,"  call codes_set(ibufr,'pack',1)\n");
    if (d->count==1)
        fprintf(self->dumper.out,"  call codes_open_file(outfile,'outfile.bufr','w')\n");
    else
        fprintf(self->dumper.out,"  call codes_open_file(outfile,'outfile.bufr','a')\n");

    fprintf(self->dumper.out,"  call codes_write(ibufr,outfile)\n");
    fprintf(self->dumper.out,"  call codes_close_file(outfile)\n");
    fprintf(self->dumper.out,"  call codes_release(ibufr)\n");
    fprintf(self->dumper.out,"  if(allocated(ivalues)) deallocate(ivalues)\n");
    fprintf(self->dumper.out,"  if(allocated(rvalues)) deallocate(rvalues)\n");
    fprintf(self->dumper.out,"  if(allocated(svalues)) deallocate(svalues)\n");
}