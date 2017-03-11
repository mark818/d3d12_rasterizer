#include "Rasterizer.h"
#include <DirectXColors.h>
#include <WinUser.h>
#include <windowsx.h>
#include <string>
using namespace std;
using namespace DirectX::Colors;

void Rasterizer::Initialize() {
  ThrowIfFailed(InitMainWindow());
  ThrowIfFailed(InitializeD3D());
  CreateCommandLineObjects();
  CreateSwapChain();
  CreateRtvAndDsvDescriptorHeaps();
  OnResize();

  ThrowIfFailed(mCommandList->Reset(mCommandAlloc.Get(), nullptr));
  BuildDescriptorHeap();
  BuildConstantBuffers();
  BuildRootSignature();
  BuildShaderAndInputLayouts();
  BuildGeometry();
  BuildPSO();

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList *lists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(1, lists);
  FlushCommandQueue();
}

void Rasterizer::Run() {
  MSG msg = { 0 };
  mTimer.Reset();
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      mTimer.Tick();
      if (!mPaused) {
        CalculateFrameStats();
        Update(mTimer);
        Draw(mTimer);
      } else {
        Sleep(100);
      }
    }
  }
}

LRESULT CALLBACK Rasterizer::MsgProc(HWND hwnd, unsigned int msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_INACTIVE) {
        mTimer.Stop();
      } else {
        mPaused = false;
        mTimer.Start();
      }
      return 0;
    case WM_SIZE:
      mClientWidth = LOWORD(lparam);
      mClientHeight = HIWORD(lparam);
      if (mDevice) {
        switch (wparam) {
          case SIZE_MINIMIZED:
            mPaused = true;
            mMinimized = true;
            mMaximized = false;
            break;
          case SIZE_MAXIMIZED:
            mPaused = false;
            mMinimized = false;
            mMaximized = true;
            OnResize();
            break;
          case SIZE_RESTORED:
            if (mMinimized) {
              mPaused = false;
              mMinimized = false;
              OnResize();
            } else if (mMaximized) {
              mPaused = false;
              mMaximized = false;
              OnResize();
            } else
              OnResize();
        }
      }
      return 0;
      // The WM_MENUCHAR message is sent when a menu is active and the user presses 
      // a key that does not correspond to any mnemonic or accelerator key. 
    case WM_MENUCHAR:
      // Don't beep when we alt-enter.
      return MAKELRESULT(0, MNC_CLOSE);
      // Catch this message so to prevent the window from becoming too small.
    case WM_GETMINMAXINFO:
      ((MINMAXINFO*)lparam)->ptMinTrackSize.x = 200;
      ((MINMAXINFO*)lparam)->ptMinTrackSize.y = 200;
      return 0;
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
      OnMouseDown(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
      OnMouseUp(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_MOUSEMOVE:
      OnMouseMove(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_KEYUP:
      if (wparam == VK_ESCAPE) {
        PostQuitMessage(0);
      } 
    case WM_DESTROY:
      PostQuitMessage(0);
      break;
    return 0;
  }

  return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK WndProc(HWND hwnd, unsigned int msg, WPARAM wparam, LPARAM lparam) {
  Rasterizer *self = reinterpret_cast<Rasterizer*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
  return self->MsgProc(hwnd, msg, wparam, lparam);
}

bool Rasterizer::InitMainWindow() {
  WNDCLASSW wc;
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.cbClsExtra = wc.cbWndExtra = 0;
  wc.hInstance = mHinst;
  wc.hIcon = LoadIcon(0, IDI_APPLICATION);
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = L"D3D12 Demo";

  if (!RegisterClassW(&wc)) {
    return false;
  }
  RECT rect{ 0, 0, mClientWidth, mClientHeight };
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;

  mHwnd = CreateWindow(L"D3D12 Demo", L"D3D12 Demo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
    width, height, nullptr, nullptr, mHinst, 0);
  if (!mHwnd) {
    return false;
  }
  
  SetWindowLongPtrA(mHwnd, GWLP_USERDATA, (LONG_PTR)this);
  ShowWindow(mHwnd, SW_SHOW);
  UpdateWindow(mHwnd);
  return true;
}

bool Rasterizer::InitializeD3D() {
  ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(mFactory.GetAddressOf())));
  HRESULT r = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(mDevice.GetAddressOf()));
  if (FAILED(r)) {
    ComPtr<IDXGIAdapter> pWarpAdaptor;
    ThrowIfFailed(mFactory->EnumWarpAdapter(IID_PPV_ARGS(pWarpAdaptor.GetAddressOf())));
    ThrowIfFailed(D3D12CreateDevice(pWarpAdaptor.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(mDevice.GetAddressOf())));
  }
  ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.GetAddressOf())));

  mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  mDsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  mCbvSrvUavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa;
  msaa.Format = mBackBufferFormat;
  msaa.SampleCount = 4;
  msaa.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  msaa.NumQualityLevels = 0;
  ThrowIfFailed(mDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof msaa));
  m4xMSAA = msaa.NumQualityLevels;
  assert(m4xMSAA > 0 && "Unexpected MSAA quality level.");
  return true;
}

void Rasterizer::CreateCommandLineObjects() {
  D3D12_COMMAND_QUEUE_DESC queue;
  queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue.NodeMask = queue.Priority = 0;
  ThrowIfFailed(mDevice->CreateCommandQueue(&queue, IID_PPV_ARGS(mCommandQueue.GetAddressOf())));

  ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandAlloc.GetAddressOf())));

  ThrowIfFailed(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
                          mCommandAlloc.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())));
  mCommandList->Close();
}

void Rasterizer::CreateSwapChain() {
  DXGI_SWAP_CHAIN_DESC sd;
  sd.BufferDesc.Width = mClientWidth;
  sd.BufferDesc.Height = mClientHeight;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferDesc.Format = mBackBufferFormat;
  sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  sd.SampleDesc.Count = m4xMSAA;
  sd.SampleDesc.Quality = m4xMSAA > 1 ? (m4xMSAA - 1) : 0;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = mSwapChainBufferCount;
  sd.OutputWindow = mHwnd;
  sd.Windowed = true;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  ThrowIfFailed(mFactory->CreateSwapChain(mCommandQueue.Get(), &sd, &mSwapChain));
}

void Rasterizer::CreateRtvAndDsvDescriptorHeaps() {
  D3D12_DESCRIPTOR_HEAP_DESC ds;
  ds.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  ds.NumDescriptors = mSwapChainBufferCount;
  ds.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ds.NodeMask = 0;
  ThrowIfFailed(mDevice->CreateDescriptorHeap(&ds, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

  ds.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  ds.NumDescriptors = 1;
  ThrowIfFailed(mDevice->CreateDescriptorHeap(&ds, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void Rasterizer::BuildDescriptorHeap() {
  D3D12_DESCRIPTOR_HEAP_DESC desc;
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 1;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  desc.NodeMask = 0;
  ThrowIfFailed(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(mCbvHeap.GetAddressOf())));
}

void Rasterizer::BuildConstantBuffers() {
  mObjectCB = make_unique<UploadBuffer<ObjectConstants>>(mDevice.Get(), 1, true);
  unsigned int byteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  D3D12_GPU_VIRTUAL_ADDRESS cbAddr = mObjectCB->Resource()->GetGPUVirtualAddress();
  D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
  desc.BufferLocation = cbAddr;
  desc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  mDevice->CreateConstantBufferView(&desc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Rasterizer::BuildRootSignature() {
  CD3DX12_ROOT_PARAMETER param[1];
  CD3DX12_DESCRIPTOR_RANGE range;
  range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
  param[0].InitAsDescriptorTable(1, &range);
  CD3DX12_ROOT_SIGNATURE_DESC desc(1, param, 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> serialized = nullptr;
  ComPtr<ID3DBlob> error = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), error.GetAddressOf());
  if (error) {
    OutputDebugStringA(reinterpret_cast<char*>(error->GetBufferPointer()));
  }
  ThrowIfFailed(hr);
  ThrowIfFailed(mDevice->CreateRootSignature(
    0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void Rasterizer::BuildShaderAndInputLayouts() {
  mVS = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
  mPS = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");
  mInputLayouts = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
  };
}

void Rasterizer::BuildGeometry() {
  std::array<Vertex, 8> vertices =
  {
    Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
    Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
    Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
    Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
    Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
    Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
    Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
    Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
  };

  std::array<std::uint16_t, 36> indices =
  {
    // front face
    0, 1, 2,
    0, 2, 3,

    // back face
    4, 6, 5,
    4, 7, 6,

    // left face
    4, 5, 1,
    4, 1, 0,

    // right face
    3, 2, 6,
    3, 6, 7,

    // top face
    1, 5, 6,
    1, 6, 2,

    // bottom face
    4, 0, 3,
    4, 3, 7
  };

  const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
  const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

  mBoxGeo = std::make_unique<MeshGeometry>();
  mBoxGeo->Name = "boxGeo";

  ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
  CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

  ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
  CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

  mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(mDevice.Get(),
    mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeo->VertexBufferUploader);

  mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(mDevice.Get(),
    mCommandList.Get(), indices.data(), ibByteSize, mBoxGeo->IndexBufferUploader);

  mBoxGeo->VertexByteStride = sizeof(Vertex);
  mBoxGeo->VertexBufferByteSize = vbByteSize;
  mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
  mBoxGeo->IndexBufferByteSize = ibByteSize;

  SubmeshGeometry submesh;
  submesh.IndexCount = (UINT)indices.size();
  submesh.StartIndexLocation = 0;
  submesh.BaseVertexLocation = 0;

  mBoxGeo->DrawArgs["box"] = submesh;
}

void Rasterizer::BuildPSO() {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = { 0 };
  desc.InputLayout = { mInputLayouts.data(), static_cast<unsigned int>(mInputLayouts.size()) };
  desc.pRootSignature = mRootSignature.Get();
  desc.VS = { reinterpret_cast<BYTE*>(mVS->GetBufferPointer()), mVS->GetBufferSize() };
  desc.PS = { reinterpret_cast<BYTE*>(mPS->GetBufferPointer()), mPS->GetBufferSize() };
  desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  desc.SampleMask = 0xffffffff;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = mBackBufferFormat;
  desc.SampleDesc.Count = m4xMSAA > 1 ? 4 : 1;
  desc.SampleDesc.Quality = m4xMSAA > 1 ? (m4xMSAA - 1) : 0;
  desc.DSVFormat = mDepthStencilFormat;
  ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(mPSO.GetAddressOf())));
}

void Rasterizer::FlushCommandQueue() {
  mCurrentFence++;
  ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));
  if (mFence->GetCompletedValue() < mCurrentFence) {
    HANDLE event = CreateEvent(0, 0, 0, 0);
    ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, event));
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
  }
}

void Rasterizer::OnResize() {
  FlushCommandQueue();
  ThrowIfFailed(mCommandList->Reset(mCommandAlloc.Get(), nullptr));
  for (int i = 0; i <mSwapChainBufferCount; i++) {
    mSwapChainBuffer[i].Reset();
  }
  mDepthStencilBuffer.Reset();
  ThrowIfFailed(mSwapChain->ResizeBuffers(mSwapChainBufferCount, mClientWidth, mClientHeight, mBackBufferFormat,
                                                                        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
  mCurrentBackBuffer = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE rvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
  for (unsigned int i = 0; i < mSwapChainBufferCount; i++) {
    ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(mSwapChainBuffer[i].GetAddressOf())));
    mDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rvHeapHandle);
    rvHeapHandle.Offset(1, mRtvDescriptorSize);
  }
  D3D12_RESOURCE_DESC depthStencilDesc;
  depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthStencilDesc.Alignment = 0;
  depthStencilDesc.Width = mClientWidth;
  depthStencilDesc.Height = mClientHeight;
  depthStencilDesc.DepthOrArraySize = 1;
  depthStencilDesc.MipLevels = 1;
  depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  depthStencilDesc.SampleDesc.Count = m4xMSAA >  1 ? 4 : 1;
  depthStencilDesc.SampleDesc.Quality = m4xMSAA > 1 ? (m4xMSAA - 1) : 0;
  depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE optClear;
  optClear.Format = mDepthStencilFormat;
  optClear.DepthStencil.Depth = 1.0f;
  optClear.DepthStencil.Stencil = 0;
  ThrowIfFailed(mDevice->CreateCommittedResource(
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
    D3D12_HEAP_FLAG_NONE,
    &depthStencilDesc,
    D3D12_RESOURCE_STATE_COMMON,
    &optClear,
    IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
  dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsvDesc.Format = mDepthStencilFormat;
  dsvDesc.Texture2D.MipSlice = 0;
  mDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

  // Transition the resource from its initial state to be used as a depth buffer.
  mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

  // Execute the resize commands.
  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  FlushCommandQueue();

  mViewport.TopLeftX = 0;
  mViewport.TopLeftY = 0;
  mViewport.Width = static_cast<float>(mClientWidth);
  mViewport.Height = static_cast<float>(mClientHeight);
  mViewport.MinDepth = 0.0f;
  mViewport.MaxDepth = 1.0f;

  mScissorRect = { 0, 0, mClientWidth, mClientHeight };

  XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
  XMStoreFloat4x4(&mProj, P);
}

void Rasterizer::Update(const GameTimer & gt) {
  // Convert Spherical to Cartesian coordinates.
  float x = mRadius*sinf(mPhi)*cosf(mTheta);
  float z = mRadius*sinf(mPhi)*sinf(mTheta);
  float y = mRadius*cosf(mPhi);

  // Build the view matrix.
  XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
  XMVECTOR target = XMVectorZero();
  XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

  XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
  XMStoreFloat4x4(&mView, view);

  XMMATRIX world = XMLoadFloat4x4(&mWorld);
  XMMATRIX proj = XMLoadFloat4x4(&mProj);
  XMMATRIX worldViewProj = world*view*proj;

  // Update the constant buffer with the latest worldViewProj matrix.
  ObjectConstants objConstants;
  XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
  mObjectCB->CopyData(0, objConstants);
}

void Rasterizer::Draw(const GameTimer & gt) {
  ThrowIfFailed(mCommandAlloc->Reset());
  ThrowIfFailed(mCommandList->Reset(mCommandAlloc.Get(), mPSO.Get()));
  mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET));
  mCommandList->RSSetScissorRects(1, &mScissorRect);
  mCommandList->RSSetViewports(1, &mViewport);
  mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::Navy, 0, nullptr);
  mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
  mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
  ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
  mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
  mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
  mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
  mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
  mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());
  mCommandList->DrawIndexedInstanced(mBoxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);
  mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT));
  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList *lists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(lists), lists);
  mSwapChain->Present(0, 0);
  mCurrentBackBuffer = (mCurrentBackBuffer + 1) % mSwapChainBufferCount;
  FlushCommandQueue();
}

void Rasterizer::OnMouseDown(WPARAM btnState, int x, int y) {
  mLastMousePos.x = x;
  mLastMousePos.y = y;

  SetCapture(mHwnd);
}

void Rasterizer::OnMouseUp(WPARAM btnState, int x, int y) {
  ReleaseCapture();
}

void Rasterizer::OnMouseMove(WPARAM btnState, int x, int y) {
  if ((btnState & MK_LBUTTON) != 0) {
    // Make each pixel correspond to a quarter of a degree.
    float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
    float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

    // Update angles based on input to orbit camera around box.
    mTheta += dx;
    mPhi += dy;

    // Restrict the angle mPhi.
    mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
  } else if ((btnState & MK_RBUTTON) != 0) {
    // Make each pixel correspond to 0.005 unit in the scene.
    float dx = 0.005f*static_cast<float>(x - mLastMousePos.x);
    float dy = 0.005f*static_cast<float>(y - mLastMousePos.y);

    // Update the camera radius based on input.
    mRadius += dx - dy;

    // Restrict the radius.
    mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
  }

  mLastMousePos.x = x;
  mLastMousePos.y = y;
}

ID3D12Resource * Rasterizer::CurrentBackBuffer() const {
  return mSwapChainBuffer[mCurrentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Rasterizer::CurrentBackBufferView() const {
  return CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), mCurrentBackBuffer, mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Rasterizer::DepthStencilView() const {
  return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Rasterizer::CalculateFrameStats() {
  // Code computes the average frames per second, and also the 
  // average time it takes to render one frame.  These stats 
  // are appended to the window caption bar.

  static int frameCnt = 0;
  static float timeElapsed = 0.0f;

  frameCnt++;

  // Compute averages over one second period.
  if ((mTimer.TotalTime() - timeElapsed) >= 1.0f) {
    float fps = (float)frameCnt; // fps = frameCnt / 1
    float mspf = 1000.0f / fps;

    string fpsStr = to_string(fps);
    string mspfStr = to_string(mspf);

    string windowText = "D3D12 Demo"
      "    fps: " + fpsStr +
      "   mspf: " + mspfStr;

    SetWindowTextA(mHwnd, windowText.c_str());

    // Reset for next average.
    frameCnt = 0;
    timeElapsed += 1.0f;
  }
}