#pragma once
#include "../Common/d3dUtil.h"
#include "../Common/GameTimer.h"
#include "../Common/UploadBuffer.h"
#include "../Common/MathHelper.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

struct Vertex {
  XMFLOAT3 Pos;
  XMFLOAT4 Color;
};

struct ObjectConstants {
  DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

class Rasterizer {
public:
  Rasterizer(HINSTANCE hinst) : mHinst(hinst), self(this) {}
  ~Rasterizer() = default;
  
  void Initialize();
  void Run();

  //Setting to public due to msg routing need
  LRESULT CALLBACK MsgProc(HWND hwnd, unsigned int msg, WPARAM wparam, LPARAM lparam);

private:
  bool InitMainWindow();
  bool InitializeD3D();
  void  CreateCommandLineObjects();
  void CreateSwapChain();
  void CreateRtvAndDsvDescriptorHeaps();
  void BuildDescriptorHeap();
  void BuildConstantBuffers();
  void BuildRootSignature();
  void BuildShaderAndInputLayouts();
  void BuildGeometry();
  void BuildPSO();

  void FlushCommandQueue();

  void OnResize();
  void Update(const GameTimer& gt);
  void Draw(const GameTimer& gt);

  void OnMouseDown(WPARAM btnState, int x, int y);
  void OnMouseUp(WPARAM btnState, int x, int y);
  void OnMouseMove(WPARAM btnState, int x, int y);

  ID3D12Resource *CurrentBackBuffer() const;
  D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

  void CalculateFrameStats();

  Rasterizer *self;
  HINSTANCE mHinst;
  GameTimer mTimer;
  bool mPaused, mMinimized, mMaximized, mResizing;
  HWND mHwnd;
  long mClientWidth = 1024, mClientHeight = 768;

  int m4xMSAA;
  static const int mSwapChainBufferCount = 2;
  int mCurrentBackBuffer = 0;
  D3D_DRIVER_TYPE mDriverType = D3D_DRIVER_TYPE_HARDWARE;
  DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

  unsigned int mRtvDescriptorSize;
  unsigned int mDsvDescriptorSize;
  unsigned int mCbvSrvUavDescriptorSize;

  unsigned long long mCurrentFence = 0;

  ComPtr<IDXGIFactory4> mFactory;
  ComPtr<ID3D12Device> mDevice;
  ComPtr<ID3D12Fence> mFence;
  ComPtr<ID3D12DescriptorHeap> mRtvHeap;
  ComPtr<ID3D12DescriptorHeap> mDsvHeap;
  ComPtr<ID3D12DescriptorHeap> mCbvHeap;
  ComPtr<ID3D12CommandQueue> mCommandQueue;
  ComPtr<ID3D12CommandAllocator> mCommandAlloc;
  ComPtr<ID3D12GraphicsCommandList> mCommandList;
  ComPtr<IDXGISwapChain> mSwapChain;
  ComPtr<ID3D12Resource> mSwapChainBuffer[mSwapChainBufferCount];
  ComPtr<ID3D12Resource> mDepthStencilBuffer;
  ComPtr<ID3D12RootSignature> mRootSignature;
  ComPtr<ID3DBlob> mVS;
  ComPtr<ID3DBlob> mPS;
  ComPtr<ID3D12PipelineState> mPSO;

  D3D12_VIEWPORT mViewport;
  RECT mScissorRect;

  std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayouts;
  std::unique_ptr<MeshGeometry> mBoxGeo;

  float mTheta = 1.5f*XM_PI;
  float mPhi = XM_PIDIV4;
  float mRadius = 5.0f;
  POINT mLastMousePos;

  XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
  XMFLOAT4X4 mView = MathHelper::Identity4x4();
  XMFLOAT4X4 mProj = MathHelper::Identity4x4();
};