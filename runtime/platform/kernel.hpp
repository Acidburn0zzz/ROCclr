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
    FGS_DEFAULT,    //!< The default kernel fine-grained system pointer support
    FGS_NO,         //!< no support of kernel fine-grained system pointer
    FGS_YES         //!< have support of kernel fine-grained system pointer
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

class KernelSignature : public HeapObject
{
private:
    std::vector<KernelParameterDescriptor> params_;
    size_t paramsSize_;
    std::string     attributes_;    //!< The kernel attributes

public:
    //! Default constructor
    KernelSignature() : paramsSize_(0) { }

    //! Construct a new signature.
    KernelSignature(
        const std::vector<KernelParameterDescriptor>& params,
        const std::string& attrib
        );

    //! Return the number of parameters
    size_t numParameters() const { return params_.size(); }

    //! Return the parameter descriptor at the given index.
    const KernelParameterDescriptor& at(size_t index) const
    {
        assert(index < params_.size() && "index is out of bounds");
        return params_[index];
    }

    //! Return the size in bytes required for the arguments on the stack.
    size_t paramsSize() const { return paramsSize_; }

    //! Return the kernel attributes
    const std::string& attributes() const { return attributes_; }
};

// @todo: look into a copy-on-write model instead of copy-on-read.
//
class KernelParameters : protected HeapObject
{
private:
    //! The signature describing these parameters.
    const KernelSignature& signature_;

    address values_;         //!< pointer to the base of the values stack.
    bool* defined_;          //!< pointer to the isDefined flags.
    bool validated_;         //!< True if all parameters are defined.
    bool* svmBound_;         //!< True at 'i' if parameter 'i' is bound to SVM pointer
    size_t execInfoOffset_;  //!< The offset of execInfo
    std::vector<void*>  execSvmPtr_; //!< The non argument svm pointers for kernel
    FGSStatus svmSystemPointersSupport_; //!< The flag for the status of the kernel
                                         //   support of fine-grain system sharing.
public:

    //! Construct a new instance of parameters for the given signature.
    KernelParameters(const KernelSignature& signature) :
        signature_(signature), validated_(false), execInfoOffset_(0), svmSystemPointersSupport_(FGS_DEFAULT)
    {
        values_    = (address) this + alignUp(sizeof(KernelParameters), 16);
        defined_   = (bool*) (values_ + signature.paramsSize());
        svmBound_ = (bool*) ((address) defined_ + signature.numParameters() * sizeof(bool));

        address limit = (address) &svmBound_[signature.numParameters()];
        ::memset(values_, '\0', limit - values_);
    }

    //! Reset the parameter at the given \a index (becomes undefined).
    void reset(size_t index)
    {
        defined_[index] = false;
        svmBound_[index] = false;
        validated_ = false;
    }
    //! Set the parameter at the given \a index to the value pointed by \a value
    // \a svmBound indicates that \a value is a SVM pointer.
    void set(size_t index, size_t size, const void* value, bool svmBound = false);

    //! Return true if the parameter at the given \a index is defined.
    bool test(size_t index) const { return defined_[index]; }

    //! Return true if all the parameters have been defined.
    bool check();

    //! The amount of memory required for local memory needed
    size_t localMemSize(size_t minDataTypeAlignment) const;

    //! Capture the state of the parameters and return the stack base pointer.
    address capture(const Device& device);
    //! Release the captured state of the parameters.
    void release(address parameters, const amd::Device& device) const;

    //! Allocate memory for this instance as well as the required storage for
    //  the values_, defined_, and svmBound_ arrays.
    void* operator new(size_t size, const KernelSignature& signature)
    {
        size_t requiredSize = alignUp(size, 16)
            + signature.paramsSize()
            + signature.numParameters() * sizeof(bool) * 2;
        return AlignedMemory::allocate(requiredSize, PARAMETERS_MIN_ALIGNMENT);
    }
    //! Deallocate the memory reserved for this instance.
    void operator delete(void * ptr) { AlignedMemory::deallocate(ptr); }

    //! Deallocate the memory reserved for this instance,
    // matching overloaded operator new.
    void operator delete(void * ptr, const KernelSignature& signature)
        { AlignedMemory::deallocate(ptr); }

    //! Returns raw kernel parameters without capture
    address values() const { return values_; }

    //! Return true if the captured parameter at the given \a index is bound to
    // SVM pointer.
    bool boundToSvmPointer(const Device& device,
                           const_address capturedAddress,
                           size_t index) const;
    //! add the svmPtr execInfo into container
    void addSvmPtr(void* const* execInfoArray, size_t count)
    {
        execSvmPtr_.clear();
        for (size_t i = 0; i < count; i++) {
            execSvmPtr_.push_back(execInfoArray[i]);
        }
    }
    //! get the number of svmPtr in the execInfo container
    size_t getNumberOfSvmPtr() const {return execSvmPtr_.size();}

    //! get the number of svmPtr in the execInfo container
    size_t getExecInfoOffset() const {return execInfoOffset_;}

    //! set the status of kernel support fine-grained SVM system pointer sharing
    void setSvmSystemPointersSupport(FGSStatus svmSystemSupport) { svmSystemPointersSupport_ = svmSystemSupport; }

    //! return the status of kernel support fine-grained SVM system pointer sharing
    FGSStatus getSvmSystemPointersSupport() const { return svmSystemPointersSupport_; }
};

/*! \brief Encapsulates a __kernel function and the argument values
 *  to be used when invoking this function.
 */
class Kernel : public RuntimeObject
{
private:
    //! The program where this kernel is defined.
    SharedReference<Program> program_;

    const Symbol&     symbol_;      //!< The symbol for this kernel.
    std::string       name_;        //!< The kernel's name.
    KernelParameters* parameters_;  //!< The parameters.

protected:
    //! Destroy this kernel
    ~Kernel();

public:
    /*! \brief Construct a kernel object from the __kernel function
     *  \a kernelName in the given \a program.
     */
    Kernel(Program& program, const Symbol& symbol, const std::string& name);

    //! Return the program containing this kernel.
    Program& program() const { return program_(); }

    //! Return this kernel's signature.
    const KernelSignature& signature() const;

    //! Return the kernel entry point for the given device.
    const device::Kernel* getDeviceKernel(
        const Device& device,   //!< Device object
        bool noAlias = true     //!< Controls alias optimization
        ) const;

    //! Return the parameters.
    KernelParameters& parameters() const { return *parameters_; }

    //! Return the kernel's name.
    const std::string& name() const { return name_; }

    virtual ObjectType objectType() const {return ObjectTypeKernel;}
};

/*! @}
 *  @}
 */

} // namespace amd

#endif /*KERNEL_HPP_*/
