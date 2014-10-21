// Copyright © 2014 Wei Wang. All Rights Reserved.
// 2014-09-10 16:34

#ifndef INCLUDE_DARRAY_DARY_H_
#define INCLUDE_DARRAY_DARY_H_

#include <glog/logging.h>

#include <utility>
#include <functional>
#include <memory>
#include <string>

#include "da/arraymath.h"
#include "da/gary.h"

namespace lapis {
class DAry {
 public:
  ~DAry();
  DAry();//alloc_size_(0),dptr_(nullptr){}
  // alloc local mem; set ga
  void Setup(int mode);
  void SetShape(const vector<int>& shape){
    shape_.Reset(shape);
  }

  /**
   * init with the same shape and partition as other,
   * if other has no partition, create a local array;
   * alloc memory may copy data
   */
  DAry(const vector<int>& shape) ;
  DAry(const DAry& other, bool copy);
  DAry(DAry&& other) ;
  DAry(const DAry& other) ;
  DAry& operator=(const DAry& other) ;
  DAry& operator=(DAry&& other) ;
  /**
    * create a new dary with data and partition from other dary,
    * but a new shape, this new shape only diff with other dary on
    * the first or last few dimension, total size should the same;
    * like Reshape();
    */
  DAry Reshape(const vector<int>& shape) ;
  /**
    * set shape and partition from proto
  void InitFromProto(const DAryProto& proto);
  void ToProto(DAryProto* proto, bool copyData);
    */

  void Allocate();
  /**
    * subdary on the 0-th dim
    */
  DAry operator[](int k) const ;

  /**
    * Dot production
    */
  void Dot( const DAry& src1, const DAry& src2, bool trans1=false, bool trans2=false);
  void Mult( const DAry& src1, const DAry& src2);
  void Mult( const DAry& src1, const float x);
  void Div( const DAry& src1, const float x);
  void Div( const DAry& src1, const DAry& x);
  /**
    * dst=src1-src2
    */
  void Minus( const DAry& src1, const DAry& src2);
  void Minus( const DAry& src, const float x) ;
  /**
    * minus this=this-src
    */
  void Minus( const DAry& src);
  /**
    * dst=src1+src2
    */
  void Add( const DAry& src1, const DAry& src2);
  /**
    * this=this+src
    */
  void Add( const DAry& src);
  void Add(const float x);
  /**
    * dst=src1+x
    */
  void Add( const DAry& src1, const float x);
 /**
    * set to 1.f if src element < t otherwise set to 0
    * Map(&mask_, [t](float v){return v<=t?1.0f:0.0f;}, mask_);
    */
  void Threshold( const DAry& src, float t);


  /**
    * generate random number between 0-1 for every element
    */
  void Random();
  void SampleGaussian(float mean, float std);
  void SampleUniform(float mean, float std);

  void Square( const DAry& src);
  void Copy( const DAry& src);
  /**
    * dst=src^x
    */
  void Pow( const DAry& src1, const float x);
  /**
    * Add the src to dst as a vector along dim-th dimension
    * i.e., the dim-th dimension should have the same length as src
    */
  void AddRow(const DAry& src);
  void AddCol(const DAry& src);

  /**
    * sum along dim-th dimension, with range r
    * # of dimensions of dst is that of src-1
    */
  void Sum( const DAry& src, const Range& r);

  /**
    * src must be a matrix
    * this is a vector
    */
  void SumRow(const DAry& src);
  void SumCol(const DAry& src);
  /**
    * sum all elements
    */
  float Sum();
  /**
    * put max(src,x) into dst
    */
  void Max( const DAry& src, float x);

  /**
    * max element
    */
  float Max();

  void Fill(const float x);
  /**
    * apply the func for every element in src, put result to dst
    */
  void Map(std::function<float(float)> func, const DAry& src);
  void Map(std::function<float(float, float)> func, const DAry& src1, const DAry& src2);
  void Map(std::function<float(float, float, float)> func, const DAry& src1, const DAry& src2,const DAry& src3);

  /**
    * return the local index range for dim-th dimension
    */
  Range IndexRange(int k) const{
    CHECK(offset_==0&&ga_!=nullptr);
    return ga_->IndexRange(k);
  }

  /**
    * fetch data to local according to index ranges
    * create a new DAry which has the same shape as this dary, but
    * the requested data are local
    */
  DAry Fetch(const vector<Range>& slice) const;
  float* FetchPtr(const vector<Range>& slice) const;
  float* FetchPtr(const Partition& part) const;
  void Delete(float* dptr)const{
    if (dptr!=dptr_)
      delete dptr;
  }

  /**
    * return the ref for the ary at this index
    * check shape
    */
  float* addr(int idx0,int idx1, int idx2, int idx3) const {
    return dptr_+locate(idx0,idx1,idx2,idx3);
  }
  float* addr(int idx0,int idx1, int idx2) const{
    return dptr_+locate(idx0,idx1,idx2);
  }
  float* addr(int idx0,int idx1) const {
    return dptr_+locate(idx0,idx1);
  }
  float* addr(int idx0) const {
    return dptr_+locate(idx0);
  }
  int locate(int idx0,int idx1, int idx2, int idx3) const {
    CHECK_EQ(shape_.dim,4);
    int pos=((idx0*shape_.s[1]+idx1)*shape_.s[2]+idx2)*shape_.s[3]+idx3;
    return pos-part_.start;
  }
  int locate(int idx0,int idx1, int idx2) const{
    CHECK_EQ(shape_.dim,3);
    int pos=(idx0*shape_.s[1]+idx1)*shape_.s[2]+idx2;
    CHECK(pos> part_.start);
    return pos-part_.start;
  }
  int locate(int idx0,int idx1) const {
    CHECK_EQ(shape_.dim,2);
    int pos=idx0*shape_.s[1]+idx1;
    CHECK(pos> part_.start);
    return pos-part_.start;
  }
  int locate(int idx0) const {
    CHECK_EQ(shape_.dim,1);
    return idx0-part_.start;
  }
  /**
    * return the value for the ary at this index
    * check shape
    */
  float& at(int idx0,int idx1, int idx2, int idx3) const {
    return dptr_[locate(idx0,idx1,idx2,idx3)];
  }
  float& at(int idx0,int idx1, int idx2) const{
    return dptr_[locate(idx0,idx1,idx2)];
  }
  float& at(int idx0,int idx1) const{
    return dptr_[locate(idx0,idx1)];
  }
  float& at(int idx0) const{
    return dptr_[locate(idx0)];
  }

  std::string ToString(bool dataonly=true){
    std::string ret;
    if(!dataonly){
      ret+=shape_.ToString();
      ret+=part_.ToString();
    }
    char buf[1024];
    sprintf(buf, "ary: ");
    for (int i = 0; i < part_.size; i++) {
      sprintf(buf+strlen(buf), "%.2f ", dptr_[i]);
    }
    return ret+std::string(buf);
  }

  const bool EmptyPartition(){
    return part_.size>0;
  }
 protected:
  int offset_;// offset to the base dary
  int alloc_size_;
  float* dptr_;
  std::shared_ptr<GAry> ga_;
  Partition part_;
  Shape shape_;
  static arraymath::ArrayMath& arymath();
};

}  // namespace lapis
#endif  // INCLUDE_DARRAY_DARY_H_
