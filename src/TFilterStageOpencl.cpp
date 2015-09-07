#include "TFilterStageOpencl.h"
#include "TFilterStageOpengl.h"



TFilterStage_OpenclKernel::TFilterStage_OpenclKernel(const std::string& Name,const std::string& KernelFilename,const std::string& KernelName,TFilter& Filter) :
	TFilterStage		( Name, Filter ),
	mKernelFilename		( KernelFilename ),
	mKernelName			( KernelName ),
	mKernelFileWatch	( KernelFilename )
{
	auto OnFileChanged = [this,&Filter](const std::string& Filename)
	{
		//	this is triggered from the main thread. But Reload() waits on opengl (also main thread...) so we deadlock...
		//	to fix this, we put it on a todo list on the filter
		auto DoReload = [this]
		{
			Reload();
			return true;
		};
		Filter.QueueJob( DoReload );
	};
	
	mKernelFileWatch.mOnChanged.AddListener( OnFileChanged );
	
	Reload();
}
	
void TFilterStage_OpenclKernel::Reload()
{
	//	delete the old ones
	mKernel.reset();
	mProgram.reset();
	
	//	load shader
	auto& Context = mFilter.GetOpenclContext();
	
	try
	{
		std::string Source;
		Soy::FileToString( mKernelFilename, Source );
		mProgram.reset( new Opencl::TProgram( Source, Context ) );
		
		//	now load kernel
		mKernel.reset( new Opencl::TKernel( mKernelName, *mProgram ) );
	}
	catch (std::exception& e)
	{
		std::Debug << "Failed to load opencl stage " << mName << ": " << e.what() << std::endl;
		mProgram.reset();
		mKernel.reset();
		return;
	}

	std::Debug << "Loaded kernel (" << mKernelName << ") okay for " << this->mName << std::endl;
	this->mOnChanged.OnTriggered(*this);
}



void TOpenclRunner::Run()
{
	//ofScopeTimerWarning Timer( (std::string("Opencl ") + this->mKernel.mKernelName).c_str(), 0 );
	auto Kernel = mKernel.Lock(mContext);

	//	get iterations we want
	Array<size_t> Iterations;
	Init( Kernel, GetArrayBridge( Iterations ) );
	
	//	divide up the iterations
	Array<Opencl::TKernelIteration> IterationSplits;
	Kernel.GetIterations( GetArrayBridge(IterationSplits), GetArrayBridge(Iterations) );

	//	for now, because buffers get realeased etc when the kernelstate is destructed,
	//	lets just block on the last execution to make sure nothing is in use. Optimise later.
	Opencl::TSync LastSemaphore;
	
	for ( int i=0;	i<IterationSplits.GetSize();	i++ )
	{
		auto& Iteration = IterationSplits[i];
		
		//	setup the iteration
		bool Block = false;
		RunIteration( Kernel, Iteration, Block );
		
		//	execute it
		Opencl::TSync ItSemaphore;
		auto* Semaphore = Block ? &ItSemaphore : nullptr;
		if ( i == IterationSplits.GetSize()-1 )
			Semaphore = &LastSemaphore;
	
		if ( Semaphore )
		{
			Kernel.QueueIteration( Iteration, *Semaphore );
			Semaphore->Wait();
		}
		else
		{
			Kernel.QueueIteration( Iteration );
		}
	}
	
	LastSemaphore.Wait();
	
	OnFinished( Kernel );
}
	


void TFilterStage_OpenclBlit::Execute(TFilterFrame& Frame,std::shared_ptr<TFilterStageRuntimeData>& Data)
{
	//	copy kernel in case it's replaced during run
	auto Kernel = mKernel;
	if ( !Soy::Assert( Kernel != nullptr, "OpenclBlut missing kernel" ) )
		return;
	auto FramePixels = Frame.GetFramePixels(mFilter,true);
	if ( !Soy::Assert( FramePixels != nullptr, "Frame missing frame pixels" ) )
		return;


	//	write straight to a texture
	//	gr: changed to write to a buffer image, if anything wants a texture, it'll convert on request
	if ( !Data )
	{
		auto CreateTexture = [&Frame,&Data,&FramePixels]
		{
			SoyPixelsMeta OutputPixelsMeta( FramePixels->GetWidth(), FramePixels->GetHeight(), SoyPixelsFormat::RGBA );
			auto* pData = new TFilterStageRuntimeData_ShaderBlit;
			Data.reset( pData );
			
			auto& StageTarget = pData->mTexture;
			if ( !StageTarget.IsValid() )
			{
				SoyPixelsMeta Meta( OutputPixelsMeta.GetWidth(), OutputPixelsMeta.GetHeight(), OutputPixelsMeta.GetFormat() );
				StageTarget = Opengl::TTexture( Meta, GL_TEXTURE_2D );
			}
		};
		
		auto CreateImageBuffer = [this,&Frame,&Data,&FramePixels]
		{
			SoyPixelsMeta OutputPixelsMeta( FramePixels->GetWidth(), FramePixels->GetHeight(), SoyPixelsFormat::RGBA );
			auto* pData = new TFilterStageRuntimeData_ShaderBlit;
			Data.reset( pData );
			
			auto& StageTarget = pData->mImageBuffer;
			if ( !StageTarget )
			{
				auto& ContextCl = mFilter.GetOpenclContext();
				SoyPixelsMeta Meta( OutputPixelsMeta.GetWidth(), OutputPixelsMeta.GetHeight(), OutputPixelsMeta.GetFormat() );
				Opencl::TSync Semaphore;

				std::stringstream BufferName;
				BufferName << this->mName << " stage output";
				std::shared_ptr<Opencl::TBufferImage> ImageBuffer( new Opencl::TBufferImage( OutputPixelsMeta, ContextCl, nullptr, OpenclBufferReadWrite::ReadWrite, BufferName.str(), &Semaphore ) );
				
				Semaphore.Wait();
				//	only assign on success
				StageTarget = ImageBuffer;
			}
		};
		
		static bool MakeTargetAsTexture = true;
		if ( MakeTargetAsTexture )
		{
			Soy::TSemaphore Semaphore;
			auto& ContextGl = mFilter.GetOpenglContext();
			ContextGl.PushJob( CreateTexture, Semaphore );
			Semaphore.Wait();
		}
		else
		{
			Soy::TSemaphore Semaphore;
			auto& ContextCl = mFilter.GetOpenclContext();
			ContextCl.PushJob( CreateImageBuffer, Semaphore );
			Semaphore.Wait();
		}
	}
	auto& StageData = dynamic_cast<TFilterStageRuntimeData_ShaderBlit&>( *Data );
	
	
	auto Init = [this,&Frame,&StageData](Opencl::TKernelState& Kernel,ArrayBridge<size_t>& Iterations)
	{
		//ofScopeTimerWarning Timer("opencl blit init",40);

		//	setup params
		for ( int u=0;	u<Kernel.mKernel.mUniforms.GetSize();	u++ )
		{
			auto& Uniform = Kernel.mKernel.mUniforms[u];
				
			if ( Frame.SetUniform( Kernel, Uniform, mFilter ) )
				continue;

			//	maybe surpress this until we need it... or only warn once
			static bool DebugUnsetUniforms = false;
			if ( DebugUnsetUniforms )
				std::Debug << "Warning; unset uniform " << Uniform.mName << std::endl;
		}
		
		//	set output depending on what we made
		if ( StageData.mTexture.IsValid(false) )
		{
			//	"frag" is output
			auto& ContextGl = mFilter.GetOpenglContext();
			Kernel.SetUniform("Frag", Opengl::TTextureAndContext( StageData.mTexture, ContextGl ), OpenclBufferReadWrite::ReadWrite );
			Iterations.PushBack( StageData.mTexture.GetWidth() );
			Iterations.PushBack( StageData.mTexture.GetHeight() );
		}
		else if ( StageData.mImageBuffer )
		{
			//	"frag" is output
			Kernel.SetUniform("Frag", *StageData.mImageBuffer );
			Iterations.PushBack( StageData.mImageBuffer->GetMeta().GetWidth() );
			Iterations.PushBack( StageData.mImageBuffer->GetMeta().GetHeight() );
		}
		else
		{
			throw Soy::AssertException("No pixel output created");
		}
	};
	
	auto Iteration = [](Opencl::TKernelState& Kernel,const Opencl::TKernelIteration& Iteration,bool& Block)
	{
		Kernel.SetUniform("OffsetX", size_cast<cl_int>(Iteration.mFirst[0]) );
		Kernel.SetUniform("OffsetY", size_cast<cl_int>(Iteration.mFirst[1]) );
	};
	
	auto Finished = [&StageData,this](Opencl::TKernelState& Kernel)
	{
		auto& ContextGl = mFilter.GetOpenglContext();
		ofScopeTimerWarning Timer("opencl blit read frag uniform",10);
		
		if ( StageData.mTexture.IsValid(false) )
		{
			Opengl::TTextureAndContext Texture( StageData.mTexture, ContextGl );
			Kernel.ReadUniform("Frag", Texture );
		}
		else if ( StageData.mImageBuffer )
		{
			Kernel.ReadUniform("Frag", *StageData.mImageBuffer );
		}
		else
		{
			throw Soy::AssertException("No pixel output created");
		}
	};
	
	//	run opencl
	auto& ContextCl = mFilter.GetOpenclContext();
	Soy::TSemaphore Semaphore;
	std::shared_ptr<PopWorker::TJob> Job( new TOpenclRunnerLambda( ContextCl, *Kernel, Init, Iteration, Finished ) );
	ContextCl.PushJob( Job, Semaphore );
	Semaphore.Wait(/*"opencl runner"*/);
}
	
