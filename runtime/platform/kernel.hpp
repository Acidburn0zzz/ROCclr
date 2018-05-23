//
// Copyright (c) 2008 Advanced Micro Devices, Inc. All rights reserved.
//

#ifndef KERNEL_HPP_
#define KERNEL_HPP_

#include "top.hpp"
#include "platform/object.hpp"

#include "amdocl/cl_kernel.h"

#include <vector>
#include <cstdlib>  // for malloc
#include <string>
#include "device/device.hpp"

enum FGSStatus {
  FGS_DEFAULT,  //!< The default kernel fine-grained system pointer support
  FGS_NO,       //!< no support of kernel fine-grained system pointer
  FGS_YES       //!< have support of kernel fine-grained system pointer
};

namespace amd {

class Symbol;
class Program;

/*! \addtogroup Runtime
 *  @{
 *
 *  \addtogroup Program Programs and Kernel functions
 *  @{
 */

class KernelSignature : public HeapObject {
 private:
  std::vector<KernelParameterDescriptor> params_;
  std::string attributes_;  //!< The kernel attributes
  uint32_t  paramsSize_;
  uint32_t  numMemories_;
  uint32_t  numSamplers_;
  uint32_t  numQueues_;

 public:
  //! Default constructor
  KernelSignature() : paramsSize_(0), numMemories_(0), numSamplers_(0), numQueues_(0) {}

  //! Construct a new signature.
  KernelSignature(const std::vector<KernelParameterDescriptor>& params, const std::string& attrib);

  //! Return the number of parameters
  size_t numParameters() const { return params_.size(); }

  //! Return the parameter descriptor at the given index.
  const KernelParameterDescriptor& at(size_t index) const {
    assert(index < params_.size() && "index is out of bounds");
    return params_[index];
  }

  std::vector<KernelParameterDescriptor>& params() { return params_; }

  //! Return the size in bytes required for the arguments on the stack.
  uint32_t paramsSize() const { return paramsSize_; }

  //! Returns the number of memory objects.
  uint32_t numMemories() const { return numMemories_; }

  //! Returns the number of sampler objects.
  uint32_t numSamplers() const { return numSamplers_; }

  //! Returns the number of queue objects.
  uint32_t numQueues() const { return numQueues_; }

  //! Return the kernel attributes
  const std::string& attributes() const { return attributes_; }
};

// @todo: look into a copy-on-write model instead of copy-on-read.
//
class KernelParameters : protected HeapObject {
 private:
  //! The signature describing these parameters.
  KernelSignature& signature_;

  address values_;                      //!< pointer to the base of the values stack.
  uint32_t execInfoOffset_;             //!< The offset of execInfo
  std::vector<void*> execSvmPtr_;       //!< The non argument svm pointers for kernel
  FGSStatus svmSystemPointersSupport_;  //!< The flag for the status of the kernel
                                        //   support of fine-grain system sharing.
  uint32_t  memoryObjOffset_;       //!< The offset of execInfo
  uint32_t  samplerObjOffset_;      //!< The offset of execInfo
  uint32_t  queueObjOffset_;        //!< The offset of execInfo
  amd::Memory** memoryObjects_;     //!< The non argument svm pointers for kernel
  amd::Sampler** samplerObjects_;   //!< The non argument svm pointers for kernel
  amd::DeviceQueue** queueObjects_; //!< The non argument svm pointers for kernel

  uint32_t  totalSize_;             //!< The total size of all captured parameters

  struct {
    uint32_t validated_ : 1;     //!< True if all parameters are defined.
    uint32_t execNewVcop_ : 1;   //!< special new VCOP for kernel execution
    uint32_t execPfpaVcop_ : 1;  //!< special PFPA VCOP for kernel execution
    uint32_t unused : 29;        //!< unused
  };

 public:
  //! Construct a new instance of parameters for the given signature.
  KernelParameters(KernelSignature& signature)
      : signature_(signature),
        execInfoOffset_(0),
        svmSystemPointersSupport_(FGS_DEFAULT),
        memoryObjects_(nullptr),
        samplerObjects_(nullptr),
        queueObjects_(nullptr),
        validated_(0),
        execNewVcop_(0),
        execPfpaVcop_(0) {
    totalSize_ = signature.paramsSize() + (signature.numMemories() +
        signature.numSamplers() + signature.numQueues()) * sizeof(void*);
    values_ = reinterpret_cast<address>(this) + alignUp(sizeof(KernelParameters), 16);
    memoryObjOffset_ = signature_.paramsSize();
    memoryObjects_ = reinterpret_cast<amd::Memory**>(values_ + memoryObjOffset_);
    samplerObjOffset_ = memoryObjOffset_ + signature_.numMemories() * sizeof(amd::Memory*);
    samplerObjects_ = reinterpret_cast<amd::Sampler**>(values_ + samplerObjOffset_);
    queueObjOffset_ = samplerObjOffset_ + signature_.numSamplers() * sizeof(amd::Sampler*);
    queueObjects_ = reinterpret_cast<amd::DeviceQueue**>(values_ + queueObjOffset_);
    address limit = reinterpret_cast<address>(&queueObjects_[signature_.numQueues()]);
    ::memset(values_, '\0', limit - values_);
  }

  explicit KernelParameters(const KernelParameters& rhs)
      : signature_(rhs.signature_),
        execInfoOffset_(rhs.execInfoOffset_),
        execSvmPtr_(rhs.execSvmPtr_),
        svmSystemPointersSupport_(rhs.svmSystemPointersSupport_),
        memoryObjects_(nullptr),
        samplerObjects_(nullptr),
        queueObjects_(nullptr),
        totalSize_(rhs.totalSize_),
        validated_(rhs.validated_),
        execNewVcop_(rhs.execNewVcop_),
        execPfpaVcop_(rhs.execPfpaVcop_) {
    values_ = reinterpret_cast<address>(this) + alignUp(sizeof(KernelParameters), 16);
    memoryObjOffset_ = signature_.paramsSize();
    memoryObjects_ = reinterpret_cast<amd::Memory**>(values_ + memoryObjOffset_);
    samplerObjOffset_ = memoryObjOffset_ + signature_.numMemories() * sizeof(amd::Memory*);
    samplerObjects_ = reinterpret_cast<amd::Sampler**>(values_ + samplerObjOffset_);
    queueObjOffset_ = samplerObjOffset_ + signature_.numSamplers() * sizeof(amd::Sampler*);
    queueObjects_ = reinterpret_cast<amd::DeviceQueue**>(values_ + queueObjOffset_);
    address limit = reinterpret_cast<address>(&queueObjects_[signature_.numQueues()]);
    ::memcpy(values_, rhs.values_, limit - values_);
  }

  //! Reset the parameter at the given \a index (becomes undefined).
  void reset(size_t index) {
    signature_.params()[index].info_.defined_ = false;
    validated_ = 0;
  }
  //! Set the parameter at the given \a index to the value pointed by \a value
  // \a svmBound indicates that \a value is a SVM pointer.
  void set(size_t index, size_t size, const void* value, bool svmBound = false);

  //! Return true if the parameter at the given \a index is defined.
  bool test(size_t index) const { return signature_.at(index).info_.defined_; }

  //! Return true if all the parameters have been defined.
  bool check();

  //! The amount of memory required for local memory needed
  size_t localMemSize(size_t minDataTypeAlignment) const;

  //! Capture the state of the parameters and return the stack base pointer.
  address capture(const Device& device);
  //! Release the captured state of the parameters.
  void release(address parameters, const amd::Device& device) const;

  //! Allocate memory for this instance as well as the required storage for
  //  the values_, defined_, and rawPointer_ arrays.
  void* operator new(size_t size, const KernelSignature& signature) {
    size_t requiredSize = alignUp(size, 16) + signature.paramsSize() +
      (signature.numMemories() + signature.numSamplers() + signature.numQueues()) *
       sizeof(void*);
    return AlignedMemory::allocate(requiredSize, PARAMETERS_MIN_ALIGNMENT);
  }
  //! Deallocate the memory reserved for this instance.
  void operator delete(void* ptr) { AlignedMemory::deallocate(ptr); }

  //! Deallocate the memory reserved for this instance,
  // matching overloaded operator new.
  void operator delete(void* ptr, const KernelSignature& signature) {
    AlignedMemory::deallocate(ptr);
  }

  //! Returns raw kernel parameters without capture
  address values() const { return values_; }

  //! Return true if the captured parameter at the given \a index is bound to
  // SVM pointer.
  bool boundToSvmPointer(const Device& device, const_address capturedAddress, size_t index) const;
  //! add the svmPtr execInfo into container
  void addSvmPtr(void* const* execInfoArray, size_t count) {
    execSvmPtr_.clear();
    for (size_t i = 0; i < count; i++) {
      execSvmPtr_.push_back(execInfoArray[i]);
    }
  }
  //! get the number of svmPtr in the execInfo container
  size_t getNumberOfSvmPtr() const { return execSvmPtr_.size(); }

  //! get the offset of svmPtr in the parameters
  uint32_t getExecInfoOffset() const { return execInfoOffset_; }

  //! get the offset of memory objects in the parameters
  uint32_t memoryObjOffset() const { return memoryObjOffset_; }

  //! get the offset of sampler objects in the parameters
  uint32_t samplerObjOffset() const { return samplerObjOffset_; }

  //! get the offset of memory objects in the parameters
  uint32_t queueObjOffset() const { return queueObjOffset_; }

  //! set the status of kernel support fine-grained SVM system pointer sharing
  void setSvmSystemPointersSupport(FGSStatus svmSystemSupport) {
    svmSystemPointersSupport_ = svmSystemSupport;
  }

  //! return the status of kernel support fine-grained SVM system pointer sharing
  FGSStatus getSvmSystemPointersSupport() const { return svmSystemPointersSupport_; }

  //! set the new VCOP in the execInfo container
  void setExecNewVcop(const bool newVcop) { execNewVcop_ = (newVcop == true); }

  //! set the PFPA VCOP in the execInfo container
  void setExecPfpaVcop(const bool pfpaVcop) { execPfpaVcop_ = (pfpaVcop == true); }

  //! get the new VCOP in the execInfo container
  bool getExecNewVcop() const { return (execNewVcop_ == 1); }

  //! get the PFPA VCOP in the execInfo container
  bool getExecPfpaVcop() const { return (execPfpaVcop_ == 1); }
};

/*! \brief Encapsulates a __kernel function and the argument values
 *  to be used when invoking this function.
 */
class Kernel : public RuntimeObject {
 private:
  //! The program where this kernel is defined.
  SharedReference<Program> program_;

  const Symbol& symbol_;          //!< The symbol for this kernel.
  std::string name_;              //!< The kernel's name.
  KernelParameters* parameters_;  //!< The parameters.

 protected:
  //! Destroy this kernel
  ~Kernel();

 public:
  /*! \brief Construct a kernel object from the __kernel function
   *  \a kernelName in the given \a program.
   */
  Kernel(Program& program, const Symbol& symbol, const std::string& name);

  //! Construct a new kernel object from an existing one. Used by CloneKernel.
  explicit Kernel(const Kernel& rhs);

  //! Return the program containing this kernel.
  Program& program() const { return program_(); }

  //! Return this kernel's signature.
  const KernelSignature& signature() const;

  //! Return the kernel entry point for the given device.
  const device::Kernel* getDeviceKernel(const Device& device,  //!< Device object
                                        bool noAlias = true    //!< Controls alias optimization
                                        ) const;

  //! Return the parameters.
  KernelParameters& parameters() const { return *parameters_; }

  //! Return the kernel's name.
  const std::string& name() const { return name_; }

  virtual ObjectType objectType() const { return ObjectTypeKernel; }
};

/*! @}
 *  @}
 */

}  // namespace amd

#endif /*KERNEL_HPP_*/
