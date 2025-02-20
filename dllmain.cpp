#include "pch.h"  // 预编译头文件，通常包含常用的库，减少编译时间
#include <d3d12.h>  // 引入 Direct3D 12 的头文件
#include <dxgi1_4.h>  // 引入 DirectX 图形接口的头文件
#include <windows.h>  // 引入 Windows API 的头文件
#include <thread>  // 引入线程库
#include <string>  // 引入字符串操作库
#include "kiero.h"  // 引入 Kiero 库，用于钩取函数
#include "imgui.h"  // 引入 ImGui 库，用于图形界面渲染
#include "imgui_impl_dx12.h"  // 引入 ImGui 的 DirectX 12 渲染实现
#include "imgui_impl_win32.h"  // 引入 ImGui 的 Windows 渲染实现



#include <windowsx.h>
// 全局变量
WNDPROC OriginalWndProc = nullptr; // 原窗口过程指针
HWND TargetWindow = nullptr;       // 目标窗口句柄

// 新窗口过程
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGuiIO& io = ImGui::GetIO(); // 获取 ImGuiIO 对象
    io.IniFilename = nullptr;//禁用配置文件保存

    switch (msg)
    {
    case WM_LBUTTONDOWN:
        io.MouseDown[0] = true; // 左键按下
        OutputDebugStringA("Left Mouse Button Clicked\n");
        break;
    case WM_LBUTTONUP:
        io.MouseDown[0] = false; // 左键释放
        OutputDebugStringA("Left Mouse Button Released\n");
        break;
    case WM_RBUTTONDOWN:
        io.MouseDown[1] = true; // 右键按下
        OutputDebugStringA("Right Mouse Button Clicked\n");
        break;
    case WM_RBUTTONUP:
        io.MouseDown[1] = false; // 右键释放
        OutputDebugStringA("Right Mouse Button Released\n");
        break;
    case WM_MBUTTONDOWN:
        io.MouseDown[2] = true; // 中键按下
        OutputDebugStringA("Middle Mouse Button Clicked\n");
        break;
    case WM_MBUTTONUP:
        io.MouseDown[2] = false; // 中键释放
        OutputDebugStringA("Middle Mouse Button Released\n");
        break;
    case WM_MOUSEMOVE:
    {
        int xPos = GET_X_LPARAM(lParam); // 获取鼠标 X 坐标
        int yPos = GET_Y_LPARAM(lParam); // 获取鼠标 Y 坐标
        io.MousePos = ImVec2((float)xPos, (float)yPos); // 更新鼠标位置
        char buffer[128];
        sprintf_s(buffer, "Mouse moved to: (%d, %d)\n", xPos, yPos);
        OutputDebugStringA(buffer);
        break;
    }
    case WM_MOUSEWHEEL:
        io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA; // 滚轮
        break;
    default:
        break;
    }

    // 调用原窗口过程处理未拦截的消息
    return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
}


// 设置子类化 Hook
bool HookWindow(HWND TargetWindow)
{
    // 获取目标窗口句柄
    //TargetWindow = FindWindow(nullptr, L"D3D12 Hello Bundles");
    if (!TargetWindow)
    {
        OutputDebugStringA("Failed to find target window.\n");
        return false;
    }

    // 设置子类化
    OriginalWndProc = (WNDPROC)SetWindowLongPtr(TargetWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
    if (!OriginalWndProc)
    {
        OutputDebugStringA("Failed to subclass the window.\n");

        return false;
    }

    OutputDebugStringA("Successfully hooked the window.\n");
    return true;
}

// 恢复原窗口过程
void UnhookWindow()
{
    if (OriginalWndProc && TargetWindow)
    {
        SetWindowLongPtrA(TargetWindow, GWLP_WNDPROC, (LONG_PTR)OriginalWndProc);
        OriginalWndProc = nullptr;
        TargetWindow = nullptr;
        OutputDebugStringA("Unhooked the window.\n");
    }
}



// 定义一个结构体用于保存每一帧的渲染上下文

/*
struct FrameContext {
    ID3D12Resource* resource = nullptr;        // 用于存储每帧的资源
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;     // 渲染目标视图描述符句柄
    ID3D12CommandAllocator* commandAllocator = nullptr;  // 命令分配器
};
*/
struct FrameContext {
    ID3D12CommandAllocator* commandAllocator; // 命令分配器
    ID3D12Resource* main_render_target_resource; // 渲染目标资源
    D3D12_CPU_DESCRIPTOR_HANDLE main_render_target_descriptor; // 渲染目标描述符
};

// 定义函数指针类型，用于存储钩取的函数地址
typedef void(__stdcall* ExecuteCommandListsD3D12)(ID3D12CommandQueue* pCommandQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
typedef HRESULT(__stdcall* PresentD3D12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
typedef void(__stdcall* DrawInstancedD12)(ID3D12GraphicsCommandList* pCommandList, UINT VertexCount, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation);
typedef void(__stdcall* DrawIndexedInstancedD12)(ID3D12GraphicsCommandList* pCommandList, UINT IndexCount, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

// 定义原始函数指针变量
ExecuteCommandListsD3D12 oExecuteCommandListsD3D12 = nullptr;  // 保存 ExecuteCommandLists 的原始函数地址
PresentD3D12 oPresentD3D12 = nullptr;  // 保存 Present 的原始函数地址
DrawInstancedD12 o_D12DrawInstanced = nullptr;  // 保存 DrawInstanced 的原始函数地址
DrawIndexedInstancedD12 o_D12DrawIndexedInstanced = nullptr;  // 保存 DrawIndexedInstanced 的原始函数地址


ID3D12CommandQueue* d3d12CommandQueue = nullptr;



static ImVec2 lastSize = ImVec2(0, 0);
bool initContext = false;
// 钩取 ExecuteCommandLists 函数，插入自定义逻辑
void __stdcall hookExecuteCommandListsD3D12(ID3D12CommandQueue* pCommandQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    if (!d3d12CommandQueue)  // 如果命令队列为空，则保存当前队列地址
        d3d12CommandQueue = pCommandQueue;

    // 调用原始的 ExecuteCommandLists 函数，执行实际的渲染命令
    oExecuteCommandListsD3D12(pCommandQueue, NumCommandLists, ppCommandLists);
}

// -------------------------------------------
// 1. 在静态作用域下，存储一系列只需要初始化一次的资源
// -------------------------------------------
//static bool                 initContext = false;
static ID3D12Device* d3d12Device = nullptr;
static ID3D12DescriptorHeap* d3d12DescriptorHeapImGuiRender = nullptr;
static ID3D12DescriptorHeap* d3d12DescriptorHeapBackBuffers = nullptr;
static ID3D12CommandAllocator* allocator = nullptr;
static ID3D12GraphicsCommandList* d3d12CommandList = nullptr;

// 为每个交换链缓冲区存储上下文
static FrameContext* frameContext = nullptr;
static int                  buffersCounts = 0;
static D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};

HWND window;


HRESULT __stdcall hookPresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
    // 如果第一次进入该函数，需要做大量初始化（ImGui、命令列表、描述符堆等）
    if (!initContext)
    {
        if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&d3d12Device)))
        {
            OutputDebugStringA("error GetDevice\n");
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }

        DXGI_SWAP_CHAIN_DESC sdesc;
        pSwapChain->GetDesc(&sdesc);
        //HWND window = sdesc.OutputWindow;
        auto window = (HWND)FindWindowA(nullptr, (LPCSTR)"Minecraft"); //Minecraft D3D12 Hello Bundles
        buffersCounts = sdesc.BufferCount;

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = buffersCounts;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 1;
        sdesc.Windowed = ((GetWindowLongPtr(window, GWL_STYLE) & WS_POPUP) != 0) ? false : true; // 判断当前窗口是否是 窗口模式 (Windowed) 或 全屏模式 (Fullscreen)
        if (FAILED(d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3d12DescriptorHeapImGuiRender))))
        {
            OutputDebugStringA("error CreateDescriptorHeap\n");
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }

        if (FAILED(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
        {
            OutputDebugStringA("error CreateCommandAllocator\n");
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }

        if (FAILED(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&d3d12CommandList))) ||
            FAILED(d3d12CommandList->Close()))
        {
            OutputDebugStringA("error CreateCommandList\n");
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = buffersCounts;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvDesc.NodeMask = 1;

        if (FAILED(d3d12Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&d3d12DescriptorHeapBackBuffers))))
        {
            OutputDebugStringA("error CreateDescriptorHeap\n");
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }
        /*
        frameContext = new FrameContext[buffersCounts];
        rtvHandle = d3d12DescriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();
        SIZE_T rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (int i = 0; i < buffersCounts; i++)
        {
            frameContext[i].commandAllocator = allocator;

            ID3D12Resource* backBuffer = nullptr;
            pSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

            d3d12Device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);
            frameContext[i].main_render_target_resource = backBuffer;
            frameContext[i].main_render_target_descriptor = rtvHandle;

            rtvHandle.ptr += rtvDescriptorSize;
            backBuffer->Release();
        }
        */

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        //io.MouseDrawCursor = true;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX12_Init(
            d3d12Device,
            buffersCounts,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            d3d12DescriptorHeapImGuiRender,
            d3d12DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart(),
            d3d12DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart()
        );

        initContext = true;
        HookWindow(window);//-----------------------
    }





    frameContext = new FrameContext[buffersCounts];
    rtvHandle = d3d12DescriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();
    SIZE_T rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (int i = 0; i < buffersCounts; i++)
    {
        frameContext[i].commandAllocator = allocator;

        ID3D12Resource* backBuffer = nullptr;
        pSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

        d3d12Device->CreateRenderTargetView(backBuffer, nullptr, rtvHandle);
        frameContext[i].main_render_target_resource = backBuffer;
        frameContext[i].main_render_target_descriptor = rtvHandle;

        rtvHandle.ptr += rtvDescriptorSize;
        backBuffer->Release();
    }

    FrameContext& currentFrameContext = frameContext[pSwapChain->GetCurrentBackBufferIndex()];
    currentFrameContext.commandAllocator->Reset();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = currentFrameContext.main_render_target_resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    d3d12CommandList->Reset(currentFrameContext.commandAllocator, nullptr);
    d3d12CommandList->ResourceBarrier(1, &barrier);
    d3d12CommandList->OMSetRenderTargets(1, &currentFrameContext.main_render_target_descriptor, FALSE, nullptr);
    d3d12CommandList->SetDescriptorHeaps(1, &d3d12DescriptorHeapImGuiRender);




    // 修复关键点：调用 NewFrame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // ImGui 界面绘制
    ImGui::Begin("Debug Window by Displaysbook"); 
    ImGui::Text("Hello, ImGui with D3D12 Hook!");
    ImGui::End();
    //ImGui::ShowDemoWindow();

    // 渲染 ImGui
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12CommandList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    d3d12CommandList->ResourceBarrier(1, &barrier);
    d3d12CommandList->Close();
    d3d12CommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&d3d12CommandList));

    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
}






// 钩取 DrawInstanced 函数，插入自定义逻辑
void __stdcall hkDrawInstancedD12(ID3D12GraphicsCommandList* pCommandList, UINT VertexCount, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    // 调用原始的 DrawInstanced 函数
    o_D12DrawInstanced(pCommandList, VertexCount, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

// 钩取 DrawIndexedInstanced 函数，插入自定义逻辑
void __stdcall hkDrawIndexedInstancedD12(ID3D12GraphicsCommandList* pCommandList, UINT IndexCount, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    // 调用原始的 DrawIndexedInstanced 函数
    o_D12DrawIndexedInstanced(pCommandList, IndexCount, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

// 初始化 Kiero 并绑定钩子
void InitializeKiero()
{
    // 初始化 Kiero 库，选择 D3D12 渲染类型
    if (kiero::init(kiero::RenderType::D3D12) == kiero::Status::Success)
    {
        // 绑定钩子到指定的函数
        kiero::bind(54, (void**)&oExecuteCommandListsD3D12, hookExecuteCommandListsD3D12);  // 绑定 ExecuteCommandLists 钩子
        kiero::bind(140, (void**)&oPresentD3D12, hookPresentD3D12);  // 绑定 Present 钩子
        kiero::bind(84, (void**)&o_D12DrawInstanced, hkDrawInstancedD12);  // 绑定 DrawInstanced 钩子
        kiero::bind(85, (void**)&o_D12DrawIndexedInstanced, hkDrawIndexedInstancedD12);  // 绑定 DrawIndexedInstanced 钩子

        OutputDebugStringA("Kiero hooks installed.\n");  // 输出钩子安装信息
    }
    else
    {
        OutputDebugStringA("Kiero initialization failed.\n");  // 输出初始化失败信息
    }
}



// DLL 主入口
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        // 创建一个新线程来执行所有复杂的初始化操作
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InitializeKiero, nullptr, 0, nullptr);
        DisableThreadLibraryCalls(hModule);  // 禁止 DLL_THREAD_ATTACH 和 DLL_THREAD_DETACH 消息

        break;

    case DLL_PROCESS_DETACH:
        // 执行清理操作
        kiero::shutdown();
        // 这里添加其他清理代码
        break;
    }

    return TRUE;
}

