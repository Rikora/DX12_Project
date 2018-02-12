#pragma once
#include <d3d12.h>
#include <wrl.h>

using namespace Microsoft::WRL;

//Utility class for all kinds of buffers
namespace dx
{
	class Buffer
	{
	public:
		Buffer(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

	public:
		void CreateVertexBuffer(const void* data, const UINT & size, const UINT & stride, ID3D12Resource** buffer, 
								ID3D12Resource** uploadHeap, D3D12_VERTEX_BUFFER_VIEW & view);
		void CreateIndexBuffer(const void* data, const UINT & size, ID3D12Resource** buffer, ID3D12Resource** uploadHeap, D3D12_INDEX_BUFFER_VIEW & view);
		void CreateConstantBufferForRoot(ID3D12Resource** buffer, UINT8** bufferAddress);

	public:
		void SetConstantBufferData(const void* data, const UINT & size, const UINT & frameIndex, UINT8** bufferAddress);
		void BindVertexBuffer(const UINT & location, D3D12_VERTEX_BUFFER_VIEW & view);
		void BindIndexBuffer(D3D12_INDEX_BUFFER_VIEW & view);
		void BindConstantBufferForRoot(const UINT & rootIndex, const UINT & frameIndex, ID3D12Resource** buffer);

	private:
		void CreateBuffer(const void* data, const UINT & size, ID3D12Resource** buffer, ID3D12Resource** uploadHeap);

	private:
		ID3D12Device* m_device;
		ID3D12GraphicsCommandList* m_commandList;
	};
}