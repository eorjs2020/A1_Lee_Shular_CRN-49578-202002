//***************************************************************************************
// ShapesApp.cpp Using a Root Descriptor instead of Descriptor Table
//For performance, there is a limit of 64 DWORDs that can be put in a root signature.
//The three types of root parameters have the following costs :
//1. Descriptor Table : 1 DWORD => the application is expected to bind a contiguous range of descriptors in a descriptor heap
//2. Root Descriptor : 2 DWORDs
//3. Root Constant : 1 DWORD per 32 - bit constant
//
//Unlike descriptor tables which require us to set a descriptor handle in a descriptor
//heap, to set a root descriptor, we simply bind the virtual address of the resource directly.
//
//There are 3 steps that needs to be changed in three methods: BuildRootSignature(), Draw(), DrawRenderItems()
// Hold down '1' key to view scene in wireframe mode.
//***************************************************************************************



#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"


using Microsoft::WRL::ComPtr;

using namespace DirectX;

using namespace DirectX::PackedVector;



const int gNumFrameResources = 3;



// Lightweight structure stores parameters to draw a shape.  This will

// vary from app-to-app.

struct RenderItem

{

	RenderItem() = default;



	// World matrix of the shape that describes the object's local space

	// relative to the world space, which defines the position, orientation,

	// and scale of the object in the world.

	XMFLOAT4X4 World = MathHelper::Identity4x4();



	// Dirty flag indicating the object data has changed and we need to update the constant buffer.

	// Because we have an object cbuffer for each FrameResource, we have to apply the

	// update to each FrameResource.  Thus, when we modify obect data we should set

	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.

	int NumFramesDirty = gNumFrameResources;



	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.

	UINT ObjCBIndex = -1;



	MeshGeometry* Geo = nullptr;



	// Primitive topology.

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;



	// DrawIndexedInstanced parameters.

	UINT IndexCount = 0;

	UINT StartIndexLocation = 0;

	int BaseVertexLocation = 0;

};



class ShapesApp : public D3DApp

{

public:

	ShapesApp(HINSTANCE hInstance);

	ShapesApp(const ShapesApp& rhs) = delete;

	ShapesApp& operator=(const ShapesApp& rhs) = delete;

	~ShapesApp();



	virtual bool Initialize()override;



private:

	virtual void OnResize()override;

	virtual void Update(const GameTimer& gt)override;

	virtual void Draw(const GameTimer& gt)override;



	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;

	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;

	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;



	void OnKeyboardInput(const GameTimer& gt);

	void UpdateCamera(const GameTimer& gt);

	void UpdateObjectCBs(const GameTimer& gt);

	void UpdateMainPassCB(const GameTimer& gt);



	void BuildDescriptorHeaps();

	void BuildConstantBufferViews();

	void BuildRootSignature();

	void BuildShadersAndInputLayout();

	void BuildShapeGeometry();

	void BuildPSOs();

	void BuildFrameResources();

	void BuildRenderItems();

	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);



private:



	std::vector<std::unique_ptr<FrameResource>> mFrameResources;

	FrameResource* mCurrFrameResource = nullptr;

	int mCurrFrameResourceIndex = 0;



	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;



	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;



	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;



	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;



	// List of all the render items.

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;



	// Render items divided by PSO.

	std::vector<RenderItem*> mOpaqueRitems;



	PassConstants mMainPassCB;



	UINT mPassCbvOffset = 0;



	bool mIsWireframe = false;



	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };

	XMFLOAT4X4 mView = MathHelper::Identity4x4();

	XMFLOAT4X4 mProj = MathHelper::Identity4x4();



	float mTheta = 1.5f * XM_PI;

	float mPhi = 0.2f * XM_PI;

	float mRadius = 15.0f;



	POINT mLastMousePos;

};



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,

	PSTR cmdLine, int showCmd)

{

	// Enable run-time memory check for debug builds.

#if defined(DEBUG) | defined(_DEBUG)

	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

#endif



	try

	{

		ShapesApp theApp(hInstance);

		if (!theApp.Initialize())

			return 0;



		return theApp.Run();

	}

	catch (DxException& e)

	{

		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);

		return 0;

	}

}



ShapesApp::ShapesApp(HINSTANCE hInstance)

	: D3DApp(hInstance)

{

}



ShapesApp::~ShapesApp()

{

	if (md3dDevice != nullptr)

		FlushCommandQueue();

}



bool ShapesApp::Initialize()

{

	if (!D3DApp::Initialize())

		return false;



	// Reset the command list to prep for initialization commands.

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));



	BuildRootSignature();

	BuildShadersAndInputLayout();

	BuildShapeGeometry();

	BuildRenderItems();

	BuildFrameResources();

	BuildDescriptorHeaps();

	BuildConstantBufferViews();

	BuildPSOs();



	// Execute the initialization commands.

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };

	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);



	// Wait until initialization is complete.

	FlushCommandQueue();



	return true;

}



void ShapesApp::OnResize()

{

	D3DApp::OnResize();



	// The window resized, so update the aspect ratio and recompute the projection matrix.

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	XMStoreFloat4x4(&mProj, P);

}



void ShapesApp::Update(const GameTimer& gt)

{

	OnKeyboardInput(gt);

	UpdateCamera(gt);



	// Cycle through the circular frame resource array.

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;

	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();



	// Has the GPU finished processing the commands of the current frame resource?

	// If not, wait until the GPU has completed commands up to this fence point.

	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)

	{

		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);

		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);

		CloseHandle(eventHandle);

	}



	UpdateObjectCBs(gt);

	UpdateMainPassCB(gt);

}



void ShapesApp::Draw(const GameTimer& gt)

{

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;


	// Reuse the memory associated with command recording.

	// We can only reset when the associated command lists have finished execution on the GPU.

	ThrowIfFailed(cmdListAlloc->Reset());


	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.

	// Reusing the command list reuses memory.

	if (mIsWireframe)

	{

		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));

	}

	else

	{

		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	}



	mCommandList->RSSetViewports(1, &mScreenViewport);

	mCommandList->RSSetScissorRects(1, &mScissorRect);



	// Indicate a state transition on the resource usage.

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),

		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	

	// Clear the back buffer and depth buffer.

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);

	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);



	// Specify the buffers we are going to render to.

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());


	//step2: No more descriptor heap,  we simply bind the virtual address of the resource directly for each frame resource.

	//ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	//mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// Bind per-pass constant buffer. We only need to do this once per - pass.

	//int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	//auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	//passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	//mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());
	//end of step2


	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);



	// Indicate a state transition on the resource usage.

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),

		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));



	// Done recording commands.

	ThrowIfFailed(mCommandList->Close());



	// Add the command list to the queue for execution.

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };

	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);



	// Swap the back and front buffers

	ThrowIfFailed(mSwapChain->Present(0, 0));

	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;



	// Advance the fence value to mark commands up to this fence point.

	mCurrFrameResource->Fence = ++mCurrentFence;



	// Add an instruction to the command queue to set a new fence point.

	// Because we are on the GPU timeline, the new fence point won't be

	// set until the GPU finishes processing all the commands prior to this Signal().

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);

}



void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)

{

	mLastMousePos.x = x;

	mLastMousePos.y = y;



	SetCapture(mhMainWnd);

}



void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)

{

	ReleaseCapture();

}



void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)

{

	if ((btnState & MK_LBUTTON) != 0)

	{

		// Make each pixel correspond to a quarter of a degree.

		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));

		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));



		// Update angles based on input to orbit camera around box.

		mTheta += dx;

		mPhi += dy;



		// Restrict the angle mPhi.

		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);

	}

	else if ((btnState & MK_RBUTTON) != 0)

	{

		// Make each pixel correspond to 0.2 unit in the scene.

		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);

		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);



		// Update the camera radius based on input.

		mRadius += dx - dy;



		// Restrict the radius.

		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);

	}



	mLastMousePos.x = x;

	mLastMousePos.y = y;

}



void ShapesApp::OnKeyboardInput(const GameTimer& gt)

{

	if (GetAsyncKeyState('1') & 0x8000)

		mIsWireframe = true;

	else

		mIsWireframe = false;

}



void ShapesApp::UpdateCamera(const GameTimer& gt)

{

	// Convert Spherical to Cartesian coordinates.

	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);

	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);

	mEyePos.y = mRadius * cosf(mPhi);



	// Build the view matrix.

	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);

	XMVECTOR target = XMVectorZero();

	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);



	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);

	XMStoreFloat4x4(&mView, view);

}



void ShapesApp::UpdateObjectCBs(const GameTimer& gt)

{

	auto currObjectCB = mCurrFrameResource->ObjectCB.get();

	for (auto& e : mAllRitems)

	{

		// Only update the cbuffer data if the constants have changed. 

		// This needs to be tracked per frame resource.

		if (e->NumFramesDirty > 0)

		{

			XMMATRIX world = XMLoadFloat4x4(&e->World);



			ObjectConstants objConstants;

			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));



			currObjectCB->CopyData(e->ObjCBIndex, objConstants);



			// Next FrameResource need to be updated too.

			e->NumFramesDirty--;

		}

	}

}



void ShapesApp::UpdateMainPassCB(const GameTimer& gt)

{

	XMMATRIX view = XMLoadFloat4x4(&mView);

	XMMATRIX proj = XMLoadFloat4x4(&mProj);



	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);

	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);

	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);



	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));

	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));

	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));

	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));

	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));

	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	mMainPassCB.EyePosW = mEyePos;

	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);

	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);

	mMainPassCB.NearZ = 1.0f;

	mMainPassCB.FarZ = 1000.0f;

	mMainPassCB.TotalTime = gt.TotalTime();

	mMainPassCB.DeltaTime = gt.DeltaTime();



	auto currPassCB = mCurrFrameResource->PassCB.get();

	currPassCB->CopyData(0, mMainPassCB);

}



void ShapesApp::BuildDescriptorHeaps()

{

	UINT objCount = (UINT)mOpaqueRitems.size();



	// Need a CBV descriptor for each object for each frame resource,

	// +1 for the perPass CBV for each frame resource.

	UINT numDescriptors = (objCount + 1) * gNumFrameResources;



	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.

	mPassCbvOffset = objCount * gNumFrameResources;



	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;

	cbvHeapDesc.NumDescriptors = numDescriptors;

	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	cbvHeapDesc.NodeMask = 0;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,

		IID_PPV_ARGS(&mCbvHeap)));

}



void ShapesApp::BuildConstantBufferViews()

{

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));



	UINT objCount = (UINT)mOpaqueRitems.size();



	// Need a CBV descriptor for each object for each frame resource.

	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)

	{

		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();

		for (UINT i = 0; i < objCount; ++i)

		{

			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();



			// Offset to the ith object constant buffer in the buffer.

			cbAddress += i * objCBByteSize;



			// Offset to the object cbv in the descriptor heap.

			int heapIndex = frameIndex * objCount + i;

			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());

			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);



			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;

			cbvDesc.BufferLocation = cbAddress;

			cbvDesc.SizeInBytes = objCBByteSize;



			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);

		}

	}



	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));



	// Last three descriptors are the pass CBVs for each frame resource.

	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)

	{

		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();



		// Offset to the pass cbv in the descriptor heap.

		int heapIndex = mPassCbvOffset + frameIndex;

		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());

		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);



		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;

		cbvDesc.BufferLocation = cbAddress;

		cbvDesc.SizeInBytes = passCBByteSize;



		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);

	}

}



void ShapesApp::BuildRootSignature()

{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	//step1: Replace Descriptor Table with Root Descriptor
	//CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	//cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	//CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	//cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.

	// Create root CBVs.

	//slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	//slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);
	slotRootParameter[0].InitAsConstantBufferView(0); // per - object CBV
	slotRootParameter[1].InitAsConstantBufferView(1); // per - pass CBV



	// A root signature is an array of root parameters.

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,

		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);



	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer

	ComPtr<ID3DBlob> serializedRootSig = nullptr;

	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,

		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());



	if (errorBlob != nullptr)

	{

		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	}

	ThrowIfFailed(hr);



	ThrowIfFailed(md3dDevice->CreateRootSignature(

		0,

		serializedRootSig->GetBufferPointer(),

		serializedRootSig->GetBufferSize(),

		IID_PPV_ARGS(mRootSignature.GetAddressOf())));

}



void ShapesApp::BuildShadersAndInputLayout()

{

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");

	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");



	mInputLayout =

	{

		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

	};

}



void ShapesApp::BuildShapeGeometry()

{

	GeometryGenerator geoGen;

	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);

	GeometryGenerator::MeshData grid = geoGen.CreateGrid(1.0f, 1.0f, 41, 41);

	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.0f, 20, 20);
	
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.0f, 1.0f, 1.0f, 20, 20);
	
	
	
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 0.5f);
	//
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 1);
	//
	GeometryGenerator::MeshData prism = geoGen.CreatePrism(1.0f, 1.0f, 1.0f, 1.0f);
	//
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 1.0f, 0);
	//
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 0);

	GeometryGenerator::MeshData torus = geoGen.CreateTorus(1.0f, 0.5f, 50, 50);



	//

	// We are concatenating all the geometry into one big vertex/index buffer.  So

	// define the regions in the buffer each submesh covers.

	//



	// Cache the vertex offsets to each object in the concatenated vertex buffer.

	UINT boxVertexOffset = 0;

	UINT gridVertexOffset = (UINT)box.Vertices.size();

	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();

	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	UINT pyramidVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	UINT coneVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();

	UINT prismVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();

	UINT diamondVertexOffset = prismVertexOffset + (UINT)prism.Vertices.size();

	UINT wedgeVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();

	UINT torusVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.

	UINT boxIndexOffset = 0;

	UINT gridIndexOffset = (UINT)box.Indices32.size();

	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();

	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	UINT pyramidIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	UINT coneIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();

	UINT prismIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();

	UINT diamondIndexOffset = prismIndexOffset + (UINT)prism.Indices32.size();

	UINT wedgeIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();

	UINT torusIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();

	// Define the SubmeshGeometry that cover different

	// regions of the vertex/index buffers.



	SubmeshGeometry boxSubmesh;

	boxSubmesh.IndexCount = (UINT)box.Indices32.size();

	boxSubmesh.StartIndexLocation = boxIndexOffset;

	boxSubmesh.BaseVertexLocation = boxVertexOffset;



	SubmeshGeometry gridSubmesh;

	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();

	gridSubmesh.StartIndexLocation = gridIndexOffset;

	gridSubmesh.BaseVertexLocation = gridVertexOffset;



	SubmeshGeometry sphereSubmesh;

	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();

	sphereSubmesh.StartIndexLocation = sphereIndexOffset;

	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;



	SubmeshGeometry cylinderSubmesh;

	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();

	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;

	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
	
	

	SubmeshGeometry pyramidSubmesh;

	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();

	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;

	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;



	SubmeshGeometry coneSubmesh;

	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();

	coneSubmesh.StartIndexLocation = coneIndexOffset;

	coneSubmesh.BaseVertexLocation = coneVertexOffset;



	SubmeshGeometry diamondSubmesh;

	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();

	diamondSubmesh.StartIndexLocation = diamondIndexOffset;

	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;



	SubmeshGeometry wedgeSubmesh;

	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();

	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;

	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;



	SubmeshGeometry prismSubmesh;

	prismSubmesh.IndexCount = (UINT)prism.Indices32.size();

	prismSubmesh.StartIndexLocation = prismIndexOffset;

	prismSubmesh.BaseVertexLocation = prismVertexOffset;


	SubmeshGeometry torusSubmesh;

	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();

	torusSubmesh.StartIndexLocation = torusIndexOffset;

	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	//

	// Extract the vertex elements we are interested in and pack the

	// vertices of all the meshes into one vertex buffer.

	//



	auto totalVertexCount =

		box.Vertices.size() +

		grid.Vertices.size() +

		sphere.Vertices.size() +

		cylinder.Vertices.size() +

		pyramid.Vertices.size() +

		cone.Vertices.size() +

		prism.Vertices.size() +

		diamond.Vertices.size() +

		wedge.Vertices.size() +

		torus.Vertices.size();



	std::vector<Vertex> vertices(totalVertexCount);



	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)

	{

		vertices[k].Pos = box.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);

	}



	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)

	{

		vertices[k].Pos = grid.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);

	}



	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)

	{

		vertices[k].Pos = sphere.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);

	}



	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)

	{

		vertices[k].Pos = cylinder.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);

	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{

		vertices[k].Pos = pyramid.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::Violet);

	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{

		vertices[k].Pos = cone.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::Firebrick);

	}

	for (size_t i = 0; i < prism.Vertices.size(); ++i, ++k)
	{

		vertices[k].Pos = prism.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGray);

	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{

		vertices[k].Pos = diamond.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::Black);

	}
	

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{

		vertices[k].Pos = wedge.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkOrange);

	}

	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{

		vertices[k].Pos = torus.Vertices[i].Position;

		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkOrange);

	}


	std::vector<std::uint16_t> indices;

	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));

	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));

	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));

	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));

	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));

	indices.insert(indices.end(), std::begin(prism.GetIndices16()), std::end(prism.GetIndices16()));

	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));

	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));

	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);



	auto geo = std::make_unique<MeshGeometry>();

	geo->Name = "shapeGeo";



	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));

	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);



	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));

	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);



	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),

		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);



	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),

		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);



	geo->VertexByteStride = sizeof(Vertex);

	geo->VertexBufferByteSize = vbByteSize;

	geo->IndexFormat = DXGI_FORMAT_R16_UINT;

	geo->IndexBufferByteSize = ibByteSize;



	geo->DrawArgs["box"] = boxSubmesh;

	geo->DrawArgs["grid"] = gridSubmesh;

	geo->DrawArgs["sphere"] = sphereSubmesh;

	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	geo->DrawArgs["pyramid"] = pyramidSubmesh;

	geo->DrawArgs["cone"] = coneSubmesh;

	geo->DrawArgs["prism"] = prismSubmesh;

	geo->DrawArgs["diamond"] = diamondSubmesh;

	geo->DrawArgs["wedge"] = wedgeSubmesh;

	geo->DrawArgs["torus"] = torusSubmesh;

	mGeometries[geo->Name] = std::move(geo);

}



void ShapesApp::BuildPSOs()

{

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;



	//

	// PSO for opaque objects.

	//

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };

	opaquePsoDesc.pRootSignature = mRootSignature.Get();

	opaquePsoDesc.VS =

	{

	 reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),

	 mShaders["standardVS"]->GetBufferSize()

	};

	opaquePsoDesc.PS =

	{

	 reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),

	 mShaders["opaquePS"]->GetBufferSize()

	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	opaquePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	opaquePsoDesc.SampleMask = UINT_MAX;

	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	opaquePsoDesc.NumRenderTargets = 1;

	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;

	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;

	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));


	//

	// PSO for opaque wireframe objects.

	//



	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;

	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));

}



void ShapesApp::BuildFrameResources()

{

	for (int i = 0; i < gNumFrameResources; ++i)

	{

		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),

			1, (UINT)mAllRitems.size()));

	}

}


void ShapesApp::BuildRenderItems()

{
	auto gridRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(40.0f, 40.0f, 40.0f) * XMMatrixTranslation(0.0f, -0.001f, 0.0f)); //add -0.001 so object dont show under grid
	
	gridRitem->ObjCBIndex = 0;

	gridRitem->Geo = mGeometries["shapeGeo"].get();

	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;

	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;

	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mAllRitems.push_back(std::move(gridRitem));
	
	// CatleWall;
	//****************************************************
	//XMMatrixRotationRollPitchYaw(0.0f, 1.0472f, 0.0f);

	// DoorCatle
	auto boxRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(1.0f, 2.0f, 15.5f) * XMMatrixTranslation(12.0f, 4.0f, 0.0f));
	
	boxRitem->ObjCBIndex = 1;

	boxRitem->Geo = mGeometries["shapeGeo"].get();

	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;

	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem));

	
	UINT objCBIndex = 33;
	for (float i = -6.0f; i <= 6.0f; i += 2.0f)
	{
		auto smallboxRitem = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&smallboxRitem->World, XMMatrixTranslation(12.0f, 5.5f, i));

		smallboxRitem->ObjCBIndex = objCBIndex++;

		smallboxRitem->Geo = mGeometries["shapeGeo"].get();

		smallboxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		smallboxRitem->IndexCount = smallboxRitem->Geo->DrawArgs["box"].IndexCount;

		smallboxRitem->StartIndexLocation = smallboxRitem->Geo->DrawArgs["box"].StartIndexLocation;

		smallboxRitem->BaseVertexLocation = smallboxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

		mAllRitems.push_back(std::move(smallboxRitem));
	}

	
	auto boxRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem2->World, XMMatrixScaling(1.0f, 5.0f, 14.5f)  * XMMatrixRotationRollPitchYaw(0.0f, 1.0472f, 0.0f) *XMMatrixTranslation(6.0f, 2.5f, -11.0f) );

	boxRitem2->ObjCBIndex = 2;

	boxRitem2->Geo = mGeometries["shapeGeo"].get();

	boxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem2->IndexCount = boxRitem2->Geo->DrawArgs["box"].IndexCount;

	boxRitem2->StartIndexLocation = boxRitem2->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem2->BaseVertexLocation = boxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem2));
	
	objCBIndex = 40;
	for (float i = -6.0f; i <= 6.0f; i += 2.0f)
	{
		auto smallboxRitem2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&smallboxRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixRotationRollPitchYaw(0.0f, 1.0472f, 0.0f) * XMMatrixTranslation(6.0f + (i * sinf(1.0472f)), 5.5f, -11.0f + (i * cosf(1.0472f))));

		smallboxRitem2->ObjCBIndex = objCBIndex++;

		smallboxRitem2->Geo = mGeometries["shapeGeo"].get();

		smallboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		smallboxRitem2->IndexCount = smallboxRitem2->Geo->DrawArgs["box"].IndexCount;

		smallboxRitem2->StartIndexLocation = smallboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;

		smallboxRitem2->BaseVertexLocation = smallboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;

		mAllRitems.push_back(std::move(smallboxRitem2));
	}
	
	auto boxRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem3->World, XMMatrixScaling(1.0f, 5.0f, 14.5f) * XMMatrixRotationRollPitchYaw(0.0f, -1.0472f, 0.0f) * XMMatrixTranslation(-6.0f, 2.5f, -11.0f) );

	boxRitem3->ObjCBIndex = 3;

	boxRitem3->Geo = mGeometries["shapeGeo"].get();

	boxRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem3->IndexCount = boxRitem3->Geo->DrawArgs["box"].IndexCount;

	boxRitem3->StartIndexLocation = boxRitem3->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem3->BaseVertexLocation = boxRitem3->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem3));
	for (float i = -6.0f; i <= 6.0f; i += 2.0f)
	{
		auto smallboxRitem2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&smallboxRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixRotationRollPitchYaw(0.0f, -1.0472f, 0.0f) * XMMatrixTranslation(-6.0f + (i * sinf(-1.0472f)), 5.5f, -11.0f + (i * cosf(-1.0472f))));

		smallboxRitem2->ObjCBIndex = objCBIndex++;

		smallboxRitem2->Geo = mGeometries["shapeGeo"].get();

		smallboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		smallboxRitem2->IndexCount = smallboxRitem2->Geo->DrawArgs["box"].IndexCount;

		smallboxRitem2->StartIndexLocation = smallboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;

		smallboxRitem2->BaseVertexLocation = smallboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;

		mAllRitems.push_back(std::move(smallboxRitem2));
	}
	
	auto boxRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem4->World, XMMatrixScaling(1.0f, 5.0f, 15.5f) * XMMatrixTranslation(-12.0f, 2.5f, 0.0f));

	boxRitem4->ObjCBIndex = 4;

	boxRitem4->Geo = mGeometries["shapeGeo"].get();

	boxRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem4->IndexCount = boxRitem4->Geo->DrawArgs["box"].IndexCount;

	boxRitem4->StartIndexLocation = boxRitem4->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem4->BaseVertexLocation = boxRitem4->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem4));
	for (float i = -6.0f; i <= 6.0f; i += 2.0f)
	{
		auto smallboxRitem2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&smallboxRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-12.0f, 5.5f, i));

		smallboxRitem2->ObjCBIndex = objCBIndex++;

		smallboxRitem2->Geo = mGeometries["shapeGeo"].get();

		smallboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		smallboxRitem2->IndexCount = smallboxRitem2->Geo->DrawArgs["box"].IndexCount;

		smallboxRitem2->StartIndexLocation = smallboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;

		smallboxRitem2->BaseVertexLocation = smallboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;

		mAllRitems.push_back(std::move(smallboxRitem2));
	}
	
	auto boxRitem5 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem5->World, XMMatrixScaling(1.0f, 5.0f, 14.5f) * XMMatrixRotationRollPitchYaw(0.0f, 1.0472f, 0.0f)* XMMatrixTranslation(-6.0f, 2.5f, 11.0f));

	boxRitem5->ObjCBIndex = 5;

	boxRitem5->Geo = mGeometries["shapeGeo"].get();

	boxRitem5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem5->IndexCount = boxRitem5->Geo->DrawArgs["box"].IndexCount;

	boxRitem5->StartIndexLocation = boxRitem5->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem5->BaseVertexLocation = boxRitem5->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem5));

	for (float i = -6.0f; i <= 6.0f; i += 2.0f)
	{
		auto smallboxRitem2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&smallboxRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationRollPitchYaw(0.0f, 1.0472f, 0.0f)* XMMatrixTranslation(-6.0f + (i * sinf(1.0472f)), 5.5f, 11.0f + (i * cosf(1.0472f))));

		smallboxRitem2->ObjCBIndex = objCBIndex++;

		smallboxRitem2->Geo = mGeometries["shapeGeo"].get();

		smallboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		smallboxRitem2->IndexCount = smallboxRitem2->Geo->DrawArgs["box"].IndexCount;

		smallboxRitem2->StartIndexLocation = smallboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;

		smallboxRitem2->BaseVertexLocation = smallboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;

		mAllRitems.push_back(std::move(smallboxRitem2));
	}
	
	auto boxRitem6 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem6->World, XMMatrixScaling(1.0f, 5.0f, 14.5f)* XMMatrixRotationRollPitchYaw(0.0f, -1.0472f, 0.0f)* XMMatrixTranslation(6.0f, 2.5f, 11.0f));

	boxRitem6->ObjCBIndex = 6;

	boxRitem6->Geo = mGeometries["shapeGeo"].get();

	boxRitem6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem6->IndexCount = boxRitem6->Geo->DrawArgs["box"].IndexCount;

	boxRitem6->StartIndexLocation = boxRitem6->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem6->BaseVertexLocation = boxRitem6->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem6));

	for (float i = -6.0f; i <= 6.0f; i += 2.0f)
	{
		auto smallboxRitem2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&smallboxRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixRotationRollPitchYaw(0.0f, -1.0472f, 0.0f) * XMMatrixTranslation(6.0f + (i * sinf(-1.0472f)), 5.5f, 11.0f + (i * cosf(-1.0472f))));

		smallboxRitem2->ObjCBIndex = objCBIndex++;

		smallboxRitem2->Geo = mGeometries["shapeGeo"].get();

		smallboxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		smallboxRitem2->IndexCount = smallboxRitem2->Geo->DrawArgs["box"].IndexCount;

		smallboxRitem2->StartIndexLocation = smallboxRitem2->Geo->DrawArgs["box"].StartIndexLocation;

		smallboxRitem2->BaseVertexLocation = smallboxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;

		mAllRitems.push_back(std::move(smallboxRitem2));
	}
	//****************************************************
	
	//Tower
	//****************************************************
	auto pyramidRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(10.0f,10.0f, 10.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(0.0f, 0.5f, 0.0f));

	pyramidRitem->ObjCBIndex = 7;

	pyramidRitem->Geo = mGeometries["shapeGeo"].get();

	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;

	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;

	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;

	mAllRitems.push_back(std::move(pyramidRitem));

	
	auto cylinderRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&cylinderRitem->World, XMMatrixScaling(2.0f, 8.0f, 2.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(0.0f, 7.5f, 0.0f));

	cylinderRitem->ObjCBIndex = 8;

	cylinderRitem->Geo = mGeometries["shapeGeo"].get();

	cylinderRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;

	cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;

	cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	mAllRitems.push_back(std::move(cylinderRitem));

	auto cylinderRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&cylinderRitem2->World, XMMatrixScaling(4.0f, 1.5f, 4.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(0.0f, 12.0f, 0.0f));

	cylinderRitem2->ObjCBIndex = 9;

	cylinderRitem2->Geo = mGeometries["shapeGeo"].get();

	cylinderRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	cylinderRitem2->IndexCount = cylinderRitem2->Geo->DrawArgs["cylinder"].IndexCount;

	cylinderRitem2->StartIndexLocation = cylinderRitem2->Geo->DrawArgs["cylinder"].StartIndexLocation;

	cylinderRitem2->BaseVertexLocation = cylinderRitem2->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	mAllRitems.push_back(std::move(cylinderRitem2));
	

	auto coneRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(5.0f, 1.5f, 5.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(0.0f, 13.5f, 0.0f));

	coneRitem->ObjCBIndex = 10;

	coneRitem->Geo = mGeometries["shapeGeo"].get();

	coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;

	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;

	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;

	mAllRitems.push_back(std::move(coneRitem));
	
	
	auto diamondRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(4.0f, 4.0f, 4.0f));

	diamondRitem->ObjCBIndex = 11;

	diamondRitem->Geo = mGeometries["shapeGeo"].get();

	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;

	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;

	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;

	mAllRitems.push_back(std::move(diamondRitem));

	auto diamondRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&diamondRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(-4.0f, 4.0f, 4.0f));

	diamondRitem2->ObjCBIndex = 12;

	diamondRitem2->Geo = mGeometries["shapeGeo"].get();

	diamondRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	diamondRitem2->IndexCount = diamondRitem2->Geo->DrawArgs["diamond"].IndexCount;

	diamondRitem2->StartIndexLocation = diamondRitem2->Geo->DrawArgs["diamond"].StartIndexLocation;

	diamondRitem2->BaseVertexLocation = diamondRitem2->Geo->DrawArgs["diamond"].BaseVertexLocation;

	mAllRitems.push_back(std::move(diamondRitem2));

	auto diamondRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&diamondRitem3->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(4.0f, 4.0f, -4.0f));

	diamondRitem3->ObjCBIndex = 13;

	diamondRitem3->Geo = mGeometries["shapeGeo"].get();

	diamondRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	diamondRitem3->IndexCount = diamondRitem3->Geo->DrawArgs["diamond"].IndexCount;

	diamondRitem3->StartIndexLocation = diamondRitem3->Geo->DrawArgs["diamond"].StartIndexLocation;

	diamondRitem3->BaseVertexLocation = diamondRitem3->Geo->DrawArgs["diamond"].BaseVertexLocation;

	mAllRitems.push_back(std::move(diamondRitem3));

	auto diamondRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&diamondRitem4->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationRollPitchYaw(0.0f, 0.785398f, 0.0f)* XMMatrixTranslation(-4.0f, 4.0f, -4.0f));

	diamondRitem4->ObjCBIndex = 14;

	diamondRitem4->Geo = mGeometries["shapeGeo"].get();

	diamondRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	diamondRitem4->IndexCount = diamondRitem4->Geo->DrawArgs["diamond"].IndexCount;

	diamondRitem4->StartIndexLocation = diamondRitem4->Geo->DrawArgs["diamond"].StartIndexLocation;

	diamondRitem4->BaseVertexLocation = diamondRitem4->Geo->DrawArgs["diamond"].BaseVertexLocation;

	mAllRitems.push_back(std::move(diamondRitem4));
	//****************************************************

	//Wall Corner 
	//****************************************************
	auto prismRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem->World, XMMatrixRotationRollPitchYaw(0.0f, -0.436332f, 0.0f)* XMMatrixScaling(2.0f, 5.0f, 2.0f)* XMMatrixTranslation(14.0f, 2.5f, -9.0f));

	prismRitem->ObjCBIndex = 15;

	prismRitem->Geo = mGeometries["shapeGeo"].get();

	prismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	prismRitem->IndexCount = prismRitem->Geo->DrawArgs["prism"].IndexCount;

	prismRitem->StartIndexLocation = prismRitem->Geo->DrawArgs["prism"].StartIndexLocation;

	prismRitem->BaseVertexLocation = prismRitem->Geo->DrawArgs["prism"].BaseVertexLocation;

	mAllRitems.push_back(std::move(prismRitem));


	auto sphereRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(14.0f, 5.5f, -9.0f));

	sphereRitem->ObjCBIndex = 16;

	sphereRitem->Geo = mGeometries["shapeGeo"].get();

	sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;

	sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;

	sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mAllRitems.push_back(std::move(sphereRitem));


	auto prismRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem2->World, XMMatrixRotationRollPitchYaw(0.0f, 0.610865f, 0.0f)* XMMatrixScaling(2.0f, 5.0f, 2.0f)* XMMatrixTranslation(-14.0f, 2.5f, 9.0f));

	prismRitem2->ObjCBIndex = 17;

	prismRitem2->Geo = mGeometries["shapeGeo"].get();

	prismRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	prismRitem2->IndexCount = prismRitem2->Geo->DrawArgs["prism"].IndexCount;

	prismRitem2->StartIndexLocation = prismRitem2->Geo->DrawArgs["prism"].StartIndexLocation;

	prismRitem2->BaseVertexLocation = prismRitem2->Geo->DrawArgs["prism"].BaseVertexLocation;

	mAllRitems.push_back(std::move(prismRitem2));


	auto sphereRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem2->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(-14.0f, 5.5f, 9.0f));

	sphereRitem2->ObjCBIndex = 18;

	sphereRitem2->Geo = mGeometries["shapeGeo"].get();

	sphereRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	sphereRitem2->IndexCount = sphereRitem2->Geo->DrawArgs["sphere"].IndexCount;

	sphereRitem2->StartIndexLocation = sphereRitem2->Geo->DrawArgs["sphere"].StartIndexLocation;

	sphereRitem2->BaseVertexLocation = sphereRitem2->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mAllRitems.push_back(std::move(sphereRitem2));


	auto prismRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem3->World, XMMatrixRotationRollPitchYaw(0.0f, -0.610865f, 0.0f)* XMMatrixScaling(2.0f, 5.0f, 2.0f)* XMMatrixTranslation(-14.0f, 2.5f, -9.0f));

	prismRitem3->ObjCBIndex = 19;

	prismRitem3->Geo = mGeometries["shapeGeo"].get();

	prismRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	prismRitem3->IndexCount = prismRitem3->Geo->DrawArgs["prism"].IndexCount;

	prismRitem3->StartIndexLocation = prismRitem3->Geo->DrawArgs["prism"].StartIndexLocation;

	prismRitem3->BaseVertexLocation = prismRitem3->Geo->DrawArgs["prism"].BaseVertexLocation;

	mAllRitems.push_back(std::move(prismRitem3));


	auto sphereRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem3->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(-14.0f, 5.5f, -9.0f));

	sphereRitem3->ObjCBIndex = 20;

	sphereRitem3->Geo = mGeometries["shapeGeo"].get();

	sphereRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	sphereRitem3->IndexCount = sphereRitem3->Geo->DrawArgs["sphere"].IndexCount;

	sphereRitem3->StartIndexLocation = sphereRitem3->Geo->DrawArgs["sphere"].StartIndexLocation;

	sphereRitem3->BaseVertexLocation = sphereRitem3->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mAllRitems.push_back(std::move(sphereRitem3));


	auto prismRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem4->World, XMMatrixRotationRollPitchYaw(0.0f, 0.436332f, 0.0f)* XMMatrixScaling(2.0f, 5.0f, 2.0f)* XMMatrixTranslation(14.0f, 2.5f, 9.0f));

	prismRitem4->ObjCBIndex = 21;

	prismRitem4->Geo = mGeometries["shapeGeo"].get();

	prismRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	prismRitem4->IndexCount = prismRitem4->Geo->DrawArgs["prism"].IndexCount;

	prismRitem4->StartIndexLocation = prismRitem4->Geo->DrawArgs["prism"].StartIndexLocation;

	prismRitem4->BaseVertexLocation = prismRitem4->Geo->DrawArgs["prism"].BaseVertexLocation;

	mAllRitems.push_back(std::move(prismRitem4));


	auto sphereRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem4->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(14.0f, 5.5f, 9.0f));

	sphereRitem4->ObjCBIndex = 22;

	sphereRitem4->Geo = mGeometries["shapeGeo"].get();

	sphereRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	sphereRitem4->IndexCount = sphereRitem4->Geo->DrawArgs["sphere"].IndexCount;

	sphereRitem4->StartIndexLocation = sphereRitem4->Geo->DrawArgs["sphere"].StartIndexLocation;

	sphereRitem4->BaseVertexLocation = sphereRitem4->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mAllRitems.push_back(std::move(sphereRitem4));


	auto prismRitem5 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem5->World, XMMatrixRotationRollPitchYaw(0.0f, -0.5f, 0.0f)* XMMatrixScaling(2.0f, 5.0f, 2.0f)* XMMatrixTranslation(0.0f, 2.5f, 17.0f));

	prismRitem5->ObjCBIndex = 23;

	prismRitem5->Geo = mGeometries["shapeGeo"].get();

	prismRitem5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	prismRitem5->IndexCount = prismRitem5->Geo->DrawArgs["prism"].IndexCount;

	prismRitem5->StartIndexLocation = prismRitem5->Geo->DrawArgs["prism"].StartIndexLocation;

	prismRitem5->BaseVertexLocation = prismRitem5->Geo->DrawArgs["prism"].BaseVertexLocation;

	mAllRitems.push_back(std::move(prismRitem5));


	auto sphereRitem5 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem5->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, 5.5f, 17.0f));

	sphereRitem5->ObjCBIndex = 24;

	sphereRitem5->Geo = mGeometries["shapeGeo"].get();

	sphereRitem5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	sphereRitem5->IndexCount = sphereRitem5->Geo->DrawArgs["sphere"].IndexCount;

	sphereRitem5->StartIndexLocation = sphereRitem5->Geo->DrawArgs["sphere"].StartIndexLocation;

	sphereRitem5->BaseVertexLocation = sphereRitem5->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mAllRitems.push_back(std::move(sphereRitem5));


	auto prismRitem6 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem6->World, XMMatrixRotationRollPitchYaw(0.0f, 0.5f, 0.0f)* XMMatrixScaling(2.0f, 5.0f, 2.0f)* XMMatrixTranslation(0.0f, 2.5f, -17.0f));

	prismRitem6->ObjCBIndex = 25;

	prismRitem6->Geo = mGeometries["shapeGeo"].get();

	prismRitem6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	prismRitem6->IndexCount = prismRitem6->Geo->DrawArgs["prism"].IndexCount;

	prismRitem6->StartIndexLocation = prismRitem6->Geo->DrawArgs["prism"].StartIndexLocation;

	prismRitem6->BaseVertexLocation = prismRitem6->Geo->DrawArgs["prism"].BaseVertexLocation;

	mAllRitems.push_back(std::move(prismRitem6));
	

	auto sphereRitem6 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem6->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, 5.5f, -17.0f));

	sphereRitem6->ObjCBIndex = 26;

	sphereRitem6->Geo = mGeometries["shapeGeo"].get();

	sphereRitem6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	sphereRitem6->IndexCount = sphereRitem6->Geo->DrawArgs["sphere"].IndexCount;

	sphereRitem6->StartIndexLocation = sphereRitem6->Geo->DrawArgs["sphere"].StartIndexLocation;

	sphereRitem6->BaseVertexLocation = sphereRitem6->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mAllRitems.push_back(std::move(sphereRitem6));
	//****************************************************

	//Wall stuff for front
	//****************************************************
	auto boxRitem7 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem7->World, XMMatrixScaling(1.0f, 5.0f, 6.5f)* XMMatrixTranslation(12.0f, 2.5f, 4.5f));

	boxRitem7->ObjCBIndex = 27;

	boxRitem7->Geo = mGeometries["shapeGeo"].get();

	boxRitem7->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem7->IndexCount = boxRitem7->Geo->DrawArgs["box"].IndexCount;

	boxRitem7->StartIndexLocation = boxRitem7->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem7->BaseVertexLocation = boxRitem7->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem7));


	auto boxRitem8 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem8->World, XMMatrixScaling(1.0f, 5.0f, 6.5f)* XMMatrixTranslation(12.0f, 2.5f, -4.5f));

	boxRitem8->ObjCBIndex = 28;

	boxRitem8->Geo = mGeometries["shapeGeo"].get();

	boxRitem8->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	boxRitem8->IndexCount = boxRitem8->Geo->DrawArgs["box"].IndexCount;

	boxRitem8->StartIndexLocation = boxRitem8->Geo->DrawArgs["box"].StartIndexLocation;

	boxRitem8->BaseVertexLocation = boxRitem8->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem8));


	auto cylinderRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&cylinderRitem3->World, XMMatrixScaling(0.1f, 4.0f, 0.1f)* XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.785398f)* XMMatrixTranslation(13.0f, 1.5f, -1.5f));

	cylinderRitem3->ObjCBIndex = 29;

	cylinderRitem3->Geo = mGeometries["shapeGeo"].get();

	cylinderRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	cylinderRitem3->IndexCount = cylinderRitem3->Geo->DrawArgs["cylinder"].IndexCount;

	cylinderRitem3->StartIndexLocation = cylinderRitem3->Geo->DrawArgs["cylinder"].StartIndexLocation;

	cylinderRitem3->BaseVertexLocation = cylinderRitem3->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	mAllRitems.push_back(std::move(cylinderRitem3));


	auto cylinderRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&cylinderRitem4->World, XMMatrixScaling(0.1f, 4.0f, 0.1f)* XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.785398f)* XMMatrixTranslation(13.0f, 1.5f, 1.5f));

	cylinderRitem4->ObjCBIndex = 30;

	cylinderRitem4->Geo = mGeometries["shapeGeo"].get();

	cylinderRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	cylinderRitem4->IndexCount = cylinderRitem4->Geo->DrawArgs["cylinder"].IndexCount;

	cylinderRitem4->StartIndexLocation = cylinderRitem4->Geo->DrawArgs["cylinder"].StartIndexLocation;

	cylinderRitem4->BaseVertexLocation = cylinderRitem4->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	mAllRitems.push_back(std::move(cylinderRitem4));
	//****************************************************

	//Eye/Torus
	//****************************************************
	auto torusRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&torusRitem->World, XMMatrixScaling(4.0f, 2.0f, 2.0f)* XMMatrixRotationRollPitchYaw(0.0f, 1.5708f, 0.0f)*  XMMatrixTranslation(0.0f, 17.5f, 0.0f));

	torusRitem->ObjCBIndex = 31;

	torusRitem->Geo = mGeometries["shapeGeo"].get();

	torusRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	torusRitem->IndexCount = torusRitem->Geo->DrawArgs["torus"].IndexCount;

	torusRitem->StartIndexLocation = torusRitem->Geo->DrawArgs["torus"].StartIndexLocation;

	torusRitem->BaseVertexLocation = torusRitem->Geo->DrawArgs["torus"].BaseVertexLocation;

	mAllRitems.push_back(std::move(torusRitem));


	auto diamondRitem5 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&diamondRitem5->World, XMMatrixScaling(1.0f, 2.0f, 1.0f)* XMMatrixTranslation(0.0f, 18.5f, 0.0f));

	diamondRitem5->ObjCBIndex = 32;

	diamondRitem5->Geo = mGeometries["shapeGeo"].get();

	diamondRitem5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	diamondRitem5->IndexCount = diamondRitem5->Geo->DrawArgs["diamond"].IndexCount;

	diamondRitem5->StartIndexLocation = diamondRitem5->Geo->DrawArgs["diamond"].StartIndexLocation;

	diamondRitem5->BaseVertexLocation = diamondRitem5->Geo->DrawArgs["diamond"].BaseVertexLocation;

	mAllRitems.push_back(std::move(diamondRitem5));
	//****************************************************
	

	// All the render items are opaque.

	for (auto& e : mAllRitems)

		mOpaqueRitems.push_back(e.get());

}



void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)

{

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));



	auto objectCB = mCurrFrameResource->ObjectCB->Resource();



	// For each render item...

	for (size_t i = 0; i < ritems.size(); ++i)

	{

		auto ri = ritems[i];



		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());

		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());

		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);


		//step 3:no more heap, we simple bind the virtual address of the resource (constant buffer) directly for each render item.

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.
		//UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
		//auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		//cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
		//cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
		objCBAddress += ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);


		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);

	}

}

