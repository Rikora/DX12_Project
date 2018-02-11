#include <graphics/D3D.hpp>
#include <graphics/SamplerStates.hpp>
#include <graphics/RootDescriptor.hpp>
#include <graphics/RootParameter.hpp>
#include <utils/Utility.hpp>
#include <assert.h>
#include <DirectXColors.h>

namespace dx
{
	void D3D::LoadShaders()
	{
		m_shaders->LoadShaders(Shaders::ID::Triangle, "src/res/shaders/VertexShader.hlsl", "src/res/shaders/FragmentShader.hlsl");
	}

	void D3D::LoadTextures()
	{
		m_texture->LoadTexture(Textures::ID::Fatboy, "src/res/textures/fatboy.png");
		m_texture->LoadTexture(Textures::ID::Smiley, "src/res/textures/smiley.png");
	}

	void D3D::LoadObjects()
	{
		m_shaders = std::make_unique<Shader>(m_device.Get(), m_commandList.Get());
		m_texture = std::make_unique<Texture>(m_device.Get(), m_commandList.Get());
		m_buffer = std::make_unique<Buffer>(m_device.Get(), m_commandList.Get());
		m_srvDescHeap = std::make_unique<DescriptorHeap>(m_device.Get(), m_commandList.Get());
		m_rootSignature = std::make_unique<RootSignature>(m_device.Get(), m_commandList.Get());
		m_model = std::make_unique<Model>(m_device.Get(), m_commandList.Get(), m_buffer.get());
	}

	void D3D::Initialize(HWND hwnd)
	{
		//Initialize DirectX12 functionality
		FindAndCreateDevice();
		CreateCommandsAndSwapChain(hwnd);
		CreateRenderTargetsAndFences();
		CreateViewportAndScissorRect();

		//Prepare scene
		LoadObjects();
		LoadShaders();
		LoadTextures();

		//Fill in the desc range and create root table for the description
		RootDescriptor srvRootDesc;
		srvRootDesc.AppendDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		srvRootDesc.AppendDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		srvRootDesc.CreateRootDescTable();

		//Fill in root parameters
		RootParameter rootParams;
		rootParams.AppendRootParameterCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);
		rootParams.AppendRootParameterDescTable(srvRootDesc.GetRootDescTable(), D3D12_SHADER_VISIBILITY_PIXEL);

		//Create a standard root signature
		m_rootSignature->CreateRootSignature(rootParams.GetRootParameters().size(), 1, &rootParams.GetRootParameters()[0], &GetStandardSamplerState(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);


		//Fill in input layout and pipeline states for shaders
		m_shaders->CreateInputLayoutAndPipelineState(Shaders::ID::Triangle, 1, m_rootSignature->GetRootSignature());

		//Create the descriptor heap that will store our SRVs
		m_srvDescHeap->CreateDescriptorHeap(2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		//Two SRVs
		m_texture->CreateSRVFromTexture(Textures::ID::Fatboy, m_srvDescHeap->GetCPUIncrementHandle(0));
		m_texture->CreateSRVFromTexture(Textures::ID::Smiley, m_srvDescHeap->GetCPUIncrementHandle(1));

		//Close the command list
		ExecuteCommandList();
		WaitForPreviousFrame();
	}

	void D3D::Render()
	{
		BeginScene(Colors::DarkGray);

		//Set resources and draw model
		m_shaders->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_srvDescHeap->SetRootDescriptorTable(1);
		m_model->BindBuffers(0, m_frameIndex);
		m_model->Draw();

		EndScene();
	}

	void D3D::BeginScene(const FLOAT* color)
	{
		//Reset resources
		assert(!m_commandAllocator->Reset());
		assert(!m_commandList->Reset(m_commandAllocator.Get(), m_shaders->GetShaders(Shaders::ID::Triangle).pipelineState.Get()));

		//Set required states
		m_rootSignature->SetRootSignature();
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_rect);

		//Indicate that the backbuffer will be used as a render target
		CD3DX12_RESOURCE_BARRIER barrier = {};
		barrier = barrier.Transition(m_backBufferRenderTarget[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &barrier);

		//Get the render target view handle for the current back buffer.
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = { 0 };
		renderTargetViewHandle = m_renderTargetViewDescHeap->GetCPUDescriptorHandleForHeapStart();
		renderTargetViewHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * m_frameIndex;

		//Record commands in the command list now.
		m_commandList->OMSetRenderTargets(1, &renderTargetViewHandle, 0, nullptr);
		m_commandList->ClearRenderTargetView(renderTargetViewHandle, color, 0, nullptr);
	}

	void D3D::EndScene()
	{
		//Indicate that the backbuffer will now be used to present
		CD3DX12_RESOURCE_BARRIER barrier = {};
		barrier = barrier.Transition(m_backBufferRenderTarget[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &barrier);

		ExecuteCommandList();
		assert(!m_swapChain->Present(0, 0));

		WaitForPreviousFrame();

		//Swap the current render target view buffer index so drawing is done on the correct buffer
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	void D3D::ShutDown()
	{
		//Close the object handle to the fence event.
		WaitForPreviousFrame();
		CloseHandle(m_fenceEvent);

		m_texture->Release();
		m_device.Get()->Release();
	}

	bool D3D::FindAndCreateDevice()
	{
		HRESULT result;
		ComPtr<IDXGIAdapter1> adapter;

		//Find suitable device
#ifdef _DEBUG
		//Enable debug layer if in debug mode
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)debugController.GetAddressOf())))
			debugController->EnableDebugLayer();
#endif
		//Create a DirectX graphics interface factory.
		result = CreateDXGIFactory1(__uuidof(IDXGIFactory5), (void**)m_factory.GetAddressOf());
		if (FAILED(result))
			return false;

		for (UINT adapterIndex = 0;; ++adapterIndex)
		{
			//Use the factory to create an adapter for the primary graphics interface (video card).
			result = m_factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf());
			if (FAILED(result))
				break;

			//Check if a device that supports feature level 12.1 is found.
			result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), (void**)m_device.GetAddressOf());
			if (SUCCEEDED(result))
				break;
		}

		if (adapter)
		{
			//Create the Direct3D 12 device
			result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), (void**)m_device.GetAddressOf());
			if (FAILED(result))
				return false;
		}
		else
		{
			//Create a Direct 3D 12 device with feature level 11.1
			result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_1, __uuidof(ID3D12Device), (void**)m_device.GetAddressOf());
			if (FAILED(result))
				return false;
		}
		return true;
	}

	void D3D::CreateRenderTargetsAndFences()
	{
		//Initialize the frameIndex to the current back buffer index
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		//Initialize the render target view heap description for the two back buffers.
		D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewHeapDesc;
		ZeroMemory(&renderTargetViewHeapDesc, sizeof(renderTargetViewHeapDesc));

		//Set the number of descriptors to two for our two back buffers.  Also set the heap type to render target views.
		renderTargetViewHeapDesc.NumDescriptors = FRAME_BUFFERS;
		renderTargetViewHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		renderTargetViewHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		//Create the render target view heap for the back buffers.
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle;
		assert(!m_device->CreateDescriptorHeap(&renderTargetViewHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)m_renderTargetViewDescHeap.GetAddressOf()));
		renderTargetViewHandle = m_renderTargetViewDescHeap->GetCPUDescriptorHandleForHeapStart();

		//Get the size of the memory location for the render target view descriptors.
		unsigned int renderTargetViewDescriptorSize;
		renderTargetViewDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		for (unsigned i = 0; i < FRAME_BUFFERS; ++i)
		{
			//Get a pointer to the back buffers from the swap chain.
			assert(!m_swapChain->GetBuffer(i, __uuidof(ID3D12Resource), (void**)m_backBufferRenderTarget[i].GetAddressOf()));
			m_device->CreateRenderTargetView(m_backBufferRenderTarget[i].Get(), nullptr, renderTargetViewHandle);
			renderTargetViewHandle.ptr += renderTargetViewDescriptorSize;
		}

		//Create an event object for the fence.
		assert(!m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf())));
		m_fenceValue = 1;
		m_fenceEvent = CreateEvent(nullptr, 0, 0, nullptr);
	}

	void D3D::CreateCommandsAndSwapChain(HWND hwnd)
	{
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc;
		ZeroMemory(&commandQueueDesc, sizeof(commandQueueDesc));
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		commandQueueDesc.NodeMask = 0;

		//Create command queue, allocator and list
		assert(!m_device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), (void**)m_commandQueue.GetAddressOf()));
		assert(!m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocator.GetAddressOf())));
		assert(!m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(m_commandList.GetAddressOf())));

		//Initialize the swap chain description.
		DXGI_SWAP_CHAIN_DESC1 scDesc = {};
		scDesc.Width = 0;
		scDesc.Height = 0;
		scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scDesc.Stereo = FALSE;
		scDesc.SampleDesc.Count = 1;
		scDesc.SampleDesc.Quality = 0;
		scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.BufferCount = FRAME_BUFFERS;
		scDesc.Scaling = DXGI_SCALING_NONE;
		scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		scDesc.Flags = 0;
		scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

		//Finally create the swap chain using the swap chain description.	
		assert(!m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(m_swapChain.GetAddressOf())));
	}

	void D3D::CreateViewportAndScissorRect()
	{
		//Fill out the Viewport
		m_viewport.TopLeftX = 0;
		m_viewport.TopLeftY = 0;
		m_viewport.Width = static_cast<float>(SCREEN_WIDTH);
		m_viewport.Height = static_cast<float>(SCREEN_HEIGHT);
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = 1.0f;

		//Fill out a scissor rect
		m_rect.left = 0;
		m_rect.top = 0;
		m_rect.right = static_cast<LONG>(SCREEN_WIDTH);
		m_rect.bottom = static_cast<LONG>(SCREEN_HEIGHT);
	}

	void D3D::WaitForPreviousFrame()
	{
		//Signal and increment the fence value.
		const UINT64 fence = m_fenceValue;
		m_commandQueue->Signal(m_fence.Get(), fence);
		m_fenceValue++;

		//Wait until command queue is done.
		if (m_fence->GetCompletedValue() < fence)
		{
			m_fence->SetEventOnCompletion(fence, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	void D3D::ExecuteCommandList()
	{
		//Load command list and execute the recorded commands
		assert(!m_commandList->Close());
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	}

	ID3D12Device* D3D::GetDevice() const
	{
		return m_device.Get();
	}

	ID3D12CommandQueue * D3D::GetCommandQueue() const
	{
		return m_commandQueue.Get();
	}

	ID3D12GraphicsCommandList* D3D::GetCommandList() const
	{
		return m_commandList.Get();
	}
}

