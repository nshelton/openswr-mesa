/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
* 
* @file builder.h
* 
* @brief Includes all the builder related functionality
* 
* Notes:
* 
******************************************************************************/

#include "builder.h"

using namespace llvm;

//////////////////////////////////////////////////////////////////////////
/// @brief Contructor for Builder.
/// @param pJitMgr - JitManager which contains modules, function passes, etc.
Builder::Builder(JitManager *pJitMgr)
    : mpJitMgr(pJitMgr)
{
    mpIRBuilder = &pJitMgr->mBuilder;

    mVoidTy = Type::getVoidTy(pJitMgr->mContext);
    mFP16Ty = Type::getHalfTy(pJitMgr->mContext);
    mFP32Ty = Type::getFloatTy(pJitMgr->mContext);
    mDoubleTy = Type::getDoubleTy(pJitMgr->mContext);
    mInt1Ty = Type::getInt1Ty(pJitMgr->mContext);
    mInt8Ty = Type::getInt8Ty(pJitMgr->mContext);
    mInt16Ty = Type::getInt16Ty(pJitMgr->mContext);
    mInt32Ty = Type::getInt32Ty(pJitMgr->mContext);
    mInt64Ty = Type::getInt64Ty(pJitMgr->mContext);
    mV4FP32Ty = StructType::get(pJitMgr->mContext, std::vector<Type*>(4, mFP32Ty), false); // vector4 float type (represented as structure)
    mV4Int32Ty = StructType::get(pJitMgr->mContext, std::vector<Type*>(4, mInt32Ty), false); // vector4 int type
    mSimdInt16Ty = VectorType::get(mInt16Ty, mpJitMgr->mVWidth);
    mSimdInt32Ty = VectorType::get(mInt32Ty, mpJitMgr->mVWidth);
    mSimdInt64Ty = VectorType::get(mInt64Ty, mpJitMgr->mVWidth);
    mSimdFP16Ty = VectorType::get(mFP16Ty, mpJitMgr->mVWidth);
    mSimdFP32Ty = VectorType::get(mFP32Ty, mpJitMgr->mVWidth);
}
