#include "TApiPanopoly.h"
#include "TApiCommon.h"

namespace ApiPanopoly
{
	const char Namespace[] = "Pop.Panopoly";

	DEFINE_BIND_FUNCTIONNAME(DepthToYuvAsync);

	void	DepthToYuvAsync(Bind::TCallback& Params);
}

namespace Panopoly
{
	class TContext;
	std::shared_ptr<TContext>	gContext;
	TContext& 					GetContext();
}


class Panopoly::TContext
{
public:
	TContext() :
		mJobQueueA("Panopoly::JobQueueA"),
		mJobQueueB("Panopoly::JobQueueB")
	{
		mJobQueueA.Start();
		mJobQueueB.Start();
	}
	
	void				QueueJob(std::function<void()> Job)
	{
		mQueueFlip++;
		auto& Queue = (mQueueFlip&1) ? mJobQueueA : mJobQueueB;
		Queue.PushJob(Job);
	}

	size_t				mQueueFlip = 0;
	SoyWorkerJobThread	mJobQueueA;
	SoyWorkerJobThread	mJobQueueB;
};


Panopoly::TContext& Panopoly::GetContext()
{
	if ( gContext )
		return *gContext;
	
	gContext.reset( new TContext() );
	return *gContext;
}


void ApiPanopoly::Bind(Bind::TContext& Context)
{
	Context.CreateGlobalObjectInstance("", Namespace);

	Context.BindGlobalFunction<BindFunction::DepthToYuvAsync>( DepthToYuvAsync, Namespace );
}


//#define TEST_OUTPUT
#include "PopDepthToYuv/Depth16ToYuv.c"


void DepthToYuv(SoyPixelsImpl& DepthPixels,SoyPixelsImpl& YuvPixels,EncodeParams_t EncodeParams)
{
	Soy::TScopeTimerPrint Timer(__PRETTY_FUNCTION__,20);
	auto Width = DepthPixels.GetWidth();
	auto Height = DepthPixels.GetHeight();
	YuvPixels.Init(SoyPixelsMeta( Width, Height, SoyPixelsFormat::Yuv_8_8_8 ));
	BufferArray<std::shared_ptr<SoyPixelsImpl>, 3> YuvPlanes;
	YuvPixels.SplitPlanes(GetArrayBridge(YuvPlanes));

	struct Meta_t
	{
		uint8_t* LumaPixels;
		uint8_t* ChromaUPixels;
		uint8_t* ChromaVPixels;
		size_t Width;
	};
	Meta_t Meta;
	Meta.LumaPixels = YuvPlanes[0]->GetPixelsArray().GetArray();
	Meta.ChromaUPixels = YuvPlanes[1]->GetPixelsArray().GetArray();
	Meta.ChromaVPixels = YuvPlanes[2]->GetPixelsArray().GetArray();
	Meta.Width = Width;
	
	auto WriteYuv = [](uint32_t x, uint32_t y, uint8_t Luma, uint8_t ChromaU, uint8_t ChromaV, void* pMeta)
	{
		auto& Meta = *reinterpret_cast<Meta_t*>(pMeta);
		auto cx = x / 2;
		auto cy = y / 2;
		auto cw = Meta.Width/2;
		auto i = x + (y*Meta.Width);
		auto ci = cx + (cy*cw);
		Meta.LumaPixels[i] = Luma;
		Meta.ChromaUPixels[ci] = ChromaU;
		Meta.ChromaVPixels[ci] = ChromaV;
		//LumaPlane.SetPixel(x, y, 0, Luma);
		//ChromaUVPlane.SetPixel(cx, cy, 0, ChromaU);
		//ChromaUVPlane.SetPixel(cx, cy, 1, ChromaV);
	};
	
	auto OnErrorCAPI = [](const char* Error,void* This)
	{
		//	todo throw exception, but needs a more complicated This for the call
		std::Debug << "Depth16ToYuv error; " << Error << std::endl;
	};
	
	if ( DepthPixels.GetFormat() == SoyPixelsFormat::Depth16mm )
	{
		auto* Depth16 = reinterpret_cast<uint16_t*>(DepthPixels.GetPixelsArray().GetArray());
		Depth16ToYuv(Depth16, Width, Height, EncodeParams, WriteYuv, OnErrorCAPI, &Meta );
		return;
	}
	else if ( DepthPixels.GetFormat() == SoyPixelsFormat::DepthFloatMetres )
	{
		auto* Depthf = reinterpret_cast<float*>(DepthPixels.GetPixelsArray().GetArray());
		DepthfToYuv(Depthf, Width, Height, EncodeParams, WriteYuv, OnErrorCAPI, &Meta );
		return;
	}

	
	std::stringstream Error;
	Error << "Unsupported pixel format " << DepthPixels.GetFormat() << " for conversion to depth yuv";
	throw Soy::AssertException(Error);
}


void ApiPanopoly::DepthToYuvAsync(Bind::TCallback& Params)
{
	//	depth image in, promise out
	auto& DepthImage = Params.GetArgumentPointer<TImageWrapper>(0);
	std::shared_ptr<SoyPixelsImpl> pDepthPixels( new SoyPixels );
	DepthImage.GetPixels(*pDepthPixels);
	
	//	get encoder params
	EncodeParams_t EncodeParams;
	EncodeParams.DepthMin = Params.GetArgumentInt(1);
	EncodeParams.DepthMax = Params.GetArgumentInt(2);
	EncodeParams.ChromaRangeCount = Params.GetArgumentInt(3);
	EncodeParams.PingPongLuma = Params.GetArgumentBool(4);
	
	//	create a promise to return
	auto pPromise = Params.mContext.CreatePromisePtr(Params.mLocalContext, __PRETTY_FUNCTION__);
	Params.Return( *pPromise );
	
	//	do conversion in a job
	auto Convert = [=]() mutable
	{
		try
		{
			pDepthPixels->Flip();
			std::shared_ptr<SoyPixelsImpl> pYuvPixels( new SoyPixels );
			DepthToYuv(*pDepthPixels,*pYuvPixels,EncodeParams);
			
			auto ResolvePromise = [=](Bind::TLocalContext& LocalContext) mutable
			{
				//	make an image and return
				BufferArray<JSValueRef,1> ConstructorArguments;
				auto ImageObject = LocalContext.mGlobalContext.CreateObjectInstance(LocalContext, TImageWrapper::GetTypeName(), GetArrayBridge(ConstructorArguments) );
				auto& Image = ImageObject.This<TImageWrapper>();
				Image.SetPixels(pYuvPixels);
				pPromise->Resolve(LocalContext, ImageObject);
			};
			auto& Context = pPromise->GetContext();
			Context.Queue(ResolvePromise);
		}
		catch(std::exception& e)
		{
			std::Debug << __PRETTY_FUNCTION__ << e.what() << std::endl;
			std::string Error = e.what();
			auto RejectPromise = [=](Bind::TLocalContext& LocalContext)
			{
				pPromise->Reject(LocalContext,Error);
			};
			auto& Context = pPromise->GetContext();
			Context.Queue(RejectPromise);
		}
	};
	auto& PanopolyContext = Panopoly::GetContext();
	PanopolyContext.QueueJob(Convert);
}


