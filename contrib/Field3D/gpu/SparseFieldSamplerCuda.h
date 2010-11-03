//----------------------------------------------------------------------------//

/*
 * Copyright (c) 2009 Sony Pictures Imageworks Inc
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.  Neither the name of Sony Pictures Imageworks nor the
 * names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//----------------------------------------------------------------------------//

#ifndef _INCLUDED_Field3D_gpu_SparseFieldSamplerCuda_H_
#define _INCLUDED_Field3D_gpu_SparseFieldSamplerCuda_H_

#include "Field3D/gpu/ns.h"
#include "Field3D/gpu/Traits.h"
#include "Field3D/Types.h"

FIELD3D_GPU_NAMESPACE_OPEN

//----------------------------------------------------------------------------//
//! discrete sampling from a dense voxel grid
template< typename VALUE_TYPE, typename SAMPLE_TYPE >
struct SparseFieldSampler : public FieldSampler
{
	typedef VALUE_TYPE value_type;
	typedef typename GpuFieldTraits< VALUE_TYPE >::cuda_value_type cuda_value_type;
	typedef SAMPLE_TYPE sample_type;
	typedef FieldSampler base;

	SparseFieldSampler( Field3D::V3i dataResolution,
	                    const Field3D::Box3i& dataWindow,
	                    int _blockCount,
	                    int _blockOrder,
						Field3D::V3i _blockRes,
						int* _blockTable,
						int _emptyValueOffset,
						cuda_value_type* _phi,
						int texMemSize )
	: base( dataResolution, dataWindow )
	, blockCount( _blockCount )
	, blockOrder( _blockOrder )
	, m_blockRes( make_int3( _blockRes.x, _blockRes.y, _blockRes.z ) )
	, m_blockXYSize( m_blockRes.x * m_blockRes.y )
	, m_blockXYZSize( m_blockRes.x * m_blockRes.y * m_blockRes.z )
	, blockTable( _blockTable )
	, m_emptyValueOffset( _emptyValueOffset )
	, phi( _phi )
	, m_texMemSize( texMemSize )
	{}

	//! get value using 1d index
	template< typename ACCESSOR >
	__host__   __device__
	SAMPLE_TYPE getValue( 	ACCESSOR& ac,
							int idx ) const
	{
		return ac( idx, phi );
	}

	//! get value using 3d index
	template< typename ACCESSOR >
	__host__   __device__
	SAMPLE_TYPE getValue( 	ACCESSOR& ac,
							int x,
							int y,
							int z ) const
	{
		int idx = getIndex( x, y, z );
		return getValue( ac, idx );
	}

	inline __host__  __device__
	void getBlockCoord( int i,
						int j,
						int k,
						int &bi,
						int &bj,
						int &bk ) const
	{
		kernel_assert( i >= 0 );
		kernel_assert( j >= 0 );
		kernel_assert( k >= 0 );
		bi = i >> blockOrder;
		bj = j >> blockOrder;
		bk = k >> blockOrder;
	}

	inline __host__  __device__
	void getVoxelInBlock( 	int i,
							int j,
							int k,
							int &vi,
							int &vj,
							int &vk ) const
	{
		kernel_assert( i >= 0 );
		kernel_assert( j >= 0 );
		kernel_assert( k >= 0 );
		vi = i & ( ( 1 << blockOrder ) - 1 );
		vj = j & ( ( 1 << blockOrder ) - 1 );
		vk = k & ( ( 1 << blockOrder ) - 1 );
	}

	inline __host__   __device__
	int blockId( 	int bi,
					int bj,
					int bk ) const
	{
		kernel_assert( bi >= 0 );
		kernel_assert( bi < m_blockRes.x );
		kernel_assert( bj >= 0 );
		kernel_assert( bj < m_blockRes.y );
		kernel_assert( bk >= 0 );
		kernel_assert( bk < m_blockRes.z );
		return bk * m_blockXYSize + bj * m_blockRes.x + bi;
	}

	//! 1d indexing inside of block
	inline __host__  __device__
	int getIndexInBlock( 	int i,
							int j,
							int k ) const
	{
		return ( k << blockOrder << blockOrder ) + ( j << blockOrder ) + i;
	}

	//! 3d to 1d index mapping
	inline __host__   __device__
	int getIndex( 	int i,
					int j,
					int k ) const
	{
		kernel_assert( i >= dataWindowMin.x );
		kernel_assert( i <= dataWindowMax.x );
		kernel_assert( j >= dataWindowMin.y );
		kernel_assert( j <= dataWindowMax.y );
		kernel_assert( k >= dataWindowMin.z );
		kernel_assert( k <= dataWindowMax.z );

		// Add crop window offset
		applyDataWindowOffset( i, j, k );
		// Find block coord
		int bi, bj, bk;
		getBlockCoord( i, j, k, bi, bj, bk );
		// Find coord in block
		int vi, vj, vk;
		getVoxelInBlock( i, j, k, vi, vj, vk );
		// Get the actual block
		int block_id = blockId( bi, bj, bk );
		kernel_assert( block_id >= 0 );
		kernel_assert( block_id < m_blockRes.x * m_blockRes.y * m_blockRes.z );
		int bt = blockTable[ block_id ];
		// Check if block data is allocated
		if ( bt > 0 )
			return bt + getIndexInBlock( vi, vj, vk );
		else
			return block_id + m_emptyValueOffset; // return index of empty value
	}

	int allocatedVoxelCount() const
	{
		return blockCount << blockOrder << blockOrder << blockOrder;
	}

	//! expose data pointer for texture binding
	cuda_value_type* dataPtr() const
	{
		return phi;
	}

	//! expose data size for texture binding
	size_t texMemSize() const
	{
		return m_texMemSize;
	}

	const int blockCount;
	const int3 m_blockRes;
	const int blockOrder;
	const int m_blockXYSize;
	const int m_blockXYZSize;

	//! data ptr
	int* blockTable;
	int m_emptyValueOffset;
	cuda_value_type* phi;
	int m_texMemSize;
};

FIELD3D_GPU_NAMESPACE_HEADER_CLOSE

#endif // Include guard
