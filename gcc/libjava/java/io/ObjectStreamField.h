
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_io_ObjectStreamField__
#define __java_io_ObjectStreamField__

#pragma interface

#include <java/lang/Object.h>

class java::io::ObjectStreamField : public ::java::lang::Object
{

public: // actually package-private
  ObjectStreamField(::java::lang::reflect::Field *);
public:
  ObjectStreamField(::java::lang::String *, ::java::lang::Class *);
  ObjectStreamField(::java::lang::String *, ::java::lang::Class *, jboolean);
public: // actually package-private
  ObjectStreamField(::java::lang::String *, ::java::lang::String *);
  virtual void resolveType(::java::lang::ClassLoader *);
public:
  virtual ::java::lang::String * getName();
  virtual ::java::lang::Class * getType();
  virtual jchar getTypeCode();
  virtual ::java::lang::String * getTypeString();
  virtual jint getOffset();
public: // actually protected
  virtual void setOffset(jint);
public:
  virtual jboolean isUnshared();
  virtual jboolean isPrimitive();
  virtual jint compareTo(::java::lang::Object *);
public: // actually package-private
  virtual void setPersistent(jboolean);
  virtual jboolean isPersistent();
  virtual void setToSet(jboolean);
  virtual jboolean isToSet();
  virtual void lookupField(::java::lang::Class *);
  virtual void checkFieldType();
public:
  virtual ::java::lang::String * toString();
public: // actually package-private
  virtual void setBooleanField(::java::lang::Object *, jboolean);
  virtual void setByteField(::java::lang::Object *, jbyte);
  virtual void setCharField(::java::lang::Object *, jchar);
  virtual void setShortField(::java::lang::Object *, jshort);
  virtual void setIntField(::java::lang::Object *, jint);
  virtual void setLongField(::java::lang::Object *, jlong);
  virtual void setFloatField(::java::lang::Object *, jfloat);
  virtual void setDoubleField(::java::lang::Object *, jdouble);
  virtual void setObjectField(::java::lang::Object *, ::java::lang::Object *);
private:
  ::java::lang::String * __attribute__((aligned(__alignof__( ::java::lang::Object)))) name;
  ::java::lang::Class * type;
  ::java::lang::String * typename$;
  jint offset;
  jboolean unshared;
  jboolean persistent;
  jboolean toset;
public: // actually package-private
  ::java::lang::reflect::Field * field;
public:
  static ::java::lang::Class class$;
};

#endif // __java_io_ObjectStreamField__