#include "TFilterStageGatherRects.h"



/*
static bool RectMatch(float4 a,float4 b)
{
	float NearEdgeDist = 50;
	
	float4 Diff = a-b;
	bool x1 = ( fabs(Diff.x) < NearEdgeDist );
	bool y1 = ( fabs(Diff.y) < NearEdgeDist );
	bool x2 = ( fabs(Diff.z) < NearEdgeDist );
	bool y2 = ( fabs(Diff.w) < NearEdgeDist );
	
	return x1 && y1 && x2 && y2;
}
*/
bool MergeRects(cl_float4& a_cl,cl_float4& b_cl,float NearEdgeDist)
{
	auto a = Soy::ClToVector( a_cl );
	auto b = Soy::ClToVector( b_cl );
	
	vec4f Diff( a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w );
	bool x1 = ( fabs(Diff.x) < NearEdgeDist );
	bool y1 = ( fabs(Diff.y) < NearEdgeDist );
	bool x2 = ( fabs(Diff.z) < NearEdgeDist );
	bool y2 = ( fabs(Diff.w) < NearEdgeDist );
	
	if ( x1 && y1 && x2 && y2 )
	{
		//	expand
		a.x = std::min( a.x, b.x );
		a.y = std::min( a.y, b.y );
		a.z = std::min( a.z, b.z );
		a.w = std::min( a.w, b.w );
		a_cl = Soy::VectorToCl( a );
		return true;
	}
	else
	{
		return false;
	}
}

bool TFilterStage_GatherRects::Execute(TFilterFrame& Frame,std::shared_ptr<TFilterStageRuntimeData>& Data)
{
	if ( !mKernel )
		return false;

	//	gr: get proper input source for kernel
	if ( !Frame.mFramePixels )
		return false;
	auto FrameWidth = Frame.mFramePixels->GetWidth();
	auto FrameHeight = Frame.mFramePixels->GetHeight();

	//	allocate data
	if ( !Data )
		Data.reset( new TFilterStageRuntimeData_GatherRects() );

	auto& StageData = *dynamic_cast<TFilterStageRuntimeData_GatherRects*>( Data.get() );
	auto& ContextCl = mFilter.GetOpenclContext();

	StageData.mRects.SetSize( 1000 );
	int RectBufferCount[] = {0};
	auto RectBufferCountArray = GetRemoteArray( RectBufferCount );
	Opencl::TBufferArray<cl_float4> RectBuffer( GetArrayBridge(StageData.mRects), ContextCl );
	Opencl::TBufferArray<cl_int> RectBufferCounter( GetArrayBridge(RectBufferCountArray), ContextCl );
	
	auto Init = [this,&Frame,&RectBuffer,&RectBufferCounter,&FrameWidth,&FrameHeight](Opencl::TKernelState& Kernel,ArrayBridge<size_t>& Iterations)
	{
		//	setup params
		for ( int u=0;	u<Kernel.mKernel.mUniforms.GetSize();	u++ )
		{
			auto& Uniform = Kernel.mKernel.mUniforms[u];
			
			if ( Frame.SetUniform( Kernel, Uniform, mFilter ) )
				continue;
		}
		
		Kernel.SetUniform("Matches", RectBuffer );
		Kernel.SetUniform("MatchesCount", RectBufferCounter );
		Kernel.SetUniform("MatchesMax", size_cast<cl_int>(RectBuffer.GetSize()) );
	
		Iterations.PushBack( FrameWidth );
		Iterations.PushBack( FrameHeight );
	};
	
	auto Iteration = [](Opencl::TKernelState& Kernel,const Opencl::TKernelIteration& Iteration,bool& Block)
	{
		Kernel.SetUniform("OffsetX", size_cast<cl_int>(Iteration.mFirst[0]) );
		Kernel.SetUniform("OffsetY", size_cast<cl_int>(Iteration.mFirst[1]) );
	};
	
	auto Finished = [&StageData,&RectBuffer,&RectBufferCounter](Opencl::TKernelState& Kernel)
	{
		cl_int RectCount = 0;
		Opencl::TSync Semaphore;
		RectBufferCounter.Read( RectCount, Kernel.GetContext(), &Semaphore );
		Semaphore.Wait();
		std::Debug << "Rect count: " << RectCount << std::endl;
		
		StageData.mRects.SetSize( std::min( RectCount, size_cast<cl_int>(RectBuffer.GetSize()) ) );
		RectBuffer.Read( GetArrayBridge(StageData.mRects), Kernel.GetContext(), &Semaphore );
		Semaphore.Wait();

	};
	
	//	run opencl
	{
		Soy::TSemaphore Semaphore;
		std::shared_ptr<PopWorker::TJob> Job( new TOpenclRunnerLambda( ContextCl, *mKernel, Init, Iteration, Finished ) );
		ContextCl.PushJob( Job, Semaphore );
		try
		{
			Semaphore.Wait();
		}
		catch (std::exception& e)
		{
			std::Debug << "Opencl stage failed: " << e.what() << std::endl;
			return false;
		}
	}
	
	//	the kernel does some merging, but due to parrallism... it doesn't get them all
	//	the remainder should be smallish, so do it ourselves
	static bool PostMerge = true;
	if ( PostMerge )
	{
		float RectMergeMax = mFilter.GetUniform("RectMergeMax").Decode<float>();
												  
		Soy::TScopeTimer Timer("Gather rects post merge",0,nullptr,true);
		auto& Rects = StageData.mRects;
		auto PreMergeCount = Rects.GetSize();
		for ( ssize_t r=Rects.GetSize()-1;	r>=0;	r-- )
		{
			bool Delete = false;
			for ( int m=0;	m<r;	m++ )
			{
				auto& Rect = Rects[r];
				auto& MatchRect = Rects[m];
				if ( MergeRects( MatchRect, Rect, RectMergeMax ) )
				{
					Delete = true;
					break;
				}
			}
			if ( !Delete )
				continue;
			Rects.RemoveBlock( r, 1 );
		}
		
		auto Time = Timer.Stop(false);
		auto RemoveCount = PreMergeCount - Rects.GetSize();
		std::Debug << mName << " post merge removed " << RemoveCount << "/" << PreMergeCount << " rects in " << Time << "ms" << std::endl;
	}
	
	
	static bool DebugAllRects = false;
	if ( DebugAllRects )
	{
		std::Debug << "Read " << StageData.mRects.GetSize() << " rects; ";
		for ( int i=0;	i<StageData.mRects.GetSize();	i++ )
		{
			std::Debug << StageData.mRects[i] << " ";
		}
		std::Debug << std::endl;
	}
	
	return true;
}



void TFilterStage_MakeRectAtlas::CreateBlitResources()
{
	auto AllocGeo = [this]
	{
		if ( mBlitGeo )
			return;

		//	make mesh
		struct TVertex
		{
			vec2f	uv;
		};
		class TMesh
		{
		public:
			TVertex	mVertexes[4];
		};
		TMesh Mesh;
		Mesh.mVertexes[0].uv = vec2f( 0, 0);
		Mesh.mVertexes[1].uv = vec2f( 1, 0);
		Mesh.mVertexes[2].uv = vec2f( 1, 1);
		Mesh.mVertexes[3].uv = vec2f( 0, 1);
		Array<GLshort> Indexes;
		Indexes.PushBack( 0 );
		Indexes.PushBack( 1 );
		Indexes.PushBack( 2 );
		
		Indexes.PushBack( 2 );
		Indexes.PushBack( 3 );
		Indexes.PushBack( 0 );
		
		//	for each part of the vertex, add an attribute to describe the overall vertex
		Opengl::TGeometryVertex Vertex;
		auto& UvAttrib = Vertex.mElements.PushBack();
		UvAttrib.mName = "TexCoord";
		UvAttrib.mType = GL_FLOAT;
		UvAttrib.mIndex = 0;	//	gr: does this matter?
		UvAttrib.mArraySize = 2;
		UvAttrib.mElementDataSize = sizeof( Mesh.mVertexes[0].uv );
		
		Array<uint8> MeshData;
		MeshData.PushBackReinterpret( Mesh );
		mBlitGeo.reset( new Opengl::TGeometry( GetArrayBridge(MeshData), GetArrayBridge(Indexes), Vertex ) );
	};
	
	auto AllocShader = [this]
	{
		if ( mBlitShader )
			return;

		
		auto VertShader =
		"uniform vec4 DstRect;\n"	//	normalised
		"attribute vec2 TexCoord;\n"
		"varying vec2 oTexCoord;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(TexCoord.x,TexCoord.y,0,1);\n"
		"   gl_Position.xy *= DstRect.zw;\n"
		"   gl_Position.xy += DstRect.xy;\n"
		//	move to view space 0..1 to -1..1
		"	gl_Position.xy *= vec2(2,2);\n"
		"	gl_Position.xy -= vec2(1,1);\n"
		"	oTexCoord = vec2(TexCoord.x,1-TexCoord.y);\n"
		"}\n";
		auto FragShader =
		"varying vec2 oTexCoord;\n"
		"uniform sampler2D SrcImage;\n"
		"uniform sampler2D MaskImage;\n"
		"uniform vec4 SrcRect;\n"	//	normalised
		"void main()\n"
		"{\n"
		"	vec2 uv = vec2( oTexCoord.x, 1-oTexCoord.y );\n"
		"   uv *= SrcRect.zw;\n"
		"   uv += SrcRect.xy;\n"
		"	float Mask = texture2D( MaskImage, uv ).y;\n"
		"	vec4 Sample = texture2D( SrcImage, uv );\n"
		"	Sample.w = Mask;\n"
		"	if ( Mask < 0.5f )	Sample = vec4(0,1,0,1);\n"
		//"	gl_FragColor = vec4(oTexCoord.x,oTexCoord.y,0,1);\n"
		"	gl_FragColor = Sample;\n"
		"}\n";
		
		auto& OpenglContext = mFilter.GetOpenglContext();
		mBlitShader.reset( new Opengl::TShader( VertShader, FragShader, mBlitGeo->mVertexDescription, "Blit shader", OpenglContext ) );
	};
	
	auto& OpenglContext = mFilter.GetOpenglContext();

	if ( mBlitGeo && mBlitShader )
		return;

	std::lock_guard<std::mutex> Lock( mBlitResourcesLock );
	try
	{
		if ( !mBlitGeo )
		{
			Soy::TSemaphore Semaphore;
			OpenglContext.PushJob( AllocGeo, Semaphore );
			Semaphore.Wait();
		}
	
		if ( !mBlitShader )
		{
			Soy::TSemaphore Semaphore;
			OpenglContext.PushJob( AllocShader, Semaphore );
			Semaphore.Wait();
		}
	}
	catch(...)
	{
		mBlitResourcesLock.unlock();
		throw;
	}
}


bool TFilterStage_MakeRectAtlas::Execute(TFilterFrame& Frame,std::shared_ptr<TFilterStageRuntimeData>& Data)
{
	//	get data
	auto pRectData = Frame.GetData( mRectsStage );
	if ( !Soy::Assert( pRectData != nullptr, "Missing image stage") )
		return false;
	auto& RectData = *dynamic_cast<TFilterStageRuntimeData_GatherRects*>( pRectData.get() );
	auto& Rects = RectData.mRects;

	auto ImageData = Frame.GetData( mImageStage );
	if ( !Soy::Assert( ImageData != nullptr, "Missing image stage") )
		return false;
	auto ImageTexture = ImageData->GetTexture();
	
	auto MaskData = Frame.GetData( mMaskStage );
	if ( !Soy::Assert( MaskData != nullptr, "Missing mask stage") )
		return false;
	auto MaskTexture = MaskData->GetTexture();
	
	//	make sure geo & shader are allocated
	CreateBlitResources();

	auto& OpenglContext = mFilter.GetOpenglContext();
	
	//	allocate output
	if ( !Data )
		Data.reset( new TFilterStageRuntimeData_MakeRectAtlas() );
	auto& StageData = *dynamic_cast<TFilterStageRuntimeData_MakeRectAtlas*>( Data.get() );
	auto& StageTexture = StageData.mTexture;
	
	//	gr: todo: calc layout first to get optimimum texture... or employ a sprite packer for efficient use
	auto Allocate = [&StageTexture]
	{
		if ( StageTexture.IsValid() )
			return;
		static SoyPixelsMeta Meta( 256, 1024, SoyPixelsFormat::RGBA );
		StageTexture = std::move( Opengl::TTexture( Meta, GL_TEXTURE_2D ) );
	};
	
	
	//	create [temp] fbo to draw to
	std::shared_ptr<Opengl::TFbo> Fbo;
	auto InitFbo = [&StageTexture,&Fbo]
	{
		Fbo.reset( new Opengl::TFbo( StageTexture ) );
		Fbo->Bind();
		Opengl::ClearColour( Soy::TRgb(0,1,0) );
		Fbo->Unbind();
	};
	Soy::TSemaphore AllocSemaphore;
	Soy::TSemaphore ClearSemaphore;
	OpenglContext.PushJob( Allocate, AllocSemaphore );
	OpenglContext.PushJob( InitFbo, ClearSemaphore );
	AllocSemaphore.Wait();
	ClearSemaphore.Wait();

	//	now blit each rect, async by hold all the waits and then waiting for them all at the end
	Array<std::shared_ptr<Soy::TSemaphore>> Waits;
	
	//	walk through each rect, generate a rect-position in the target, make a blit job, and move on!
	vec2x<size_t> Border( 1, 1 );
	size_t RowHeight = 0;
	size_t RectLeft = Border.x;
	size_t RectTop = Border.y;
	auto TargetWidth = StageTexture.GetWidth();
	auto TargetHeight = StageTexture.GetHeight();

	Array<Soy::Rectf> RectNewRects;
	
	bool FilledTexture = false;
	for ( int i=0;	i<Rects.GetSize();	i++ )
	{
		auto SourceRect4 = Soy::ClToVector( Rects[i] );
		Soy::Rectf SourceRect( SourceRect4.x, SourceRect4.y, SourceRect4.z-SourceRect4.x, SourceRect4.w-SourceRect4.y );
		
		//	work out rect where this will go
		Soy::Rectf DestRect( RectLeft, RectTop, SourceRect.w, SourceRect.h );
		if ( DestRect.Right() > TargetWidth )
		{
			//	move to next line
			RectTop += RowHeight + Border.y;
			RowHeight = SourceRect.h;
			RectLeft = 0;
			DestRect = Soy::Rectf( RectLeft, RectTop, SourceRect.w, SourceRect.h );
			
			//	gone off the texture!
			if ( DestRect.Bottom() > TargetHeight )
			{
				FilledTexture = true;
				break;
			}
		}
		
		//	do blit
		auto Blit = [this,&Fbo,&StageData,SourceRect,DestRect,&ImageTexture,&MaskTexture]
		{
			Fbo->Bind();
			auto Shader = mBlitShader->Bind();
			auto SourceRectv = Soy::RectToVector( SourceRect );
			auto DestRectv = Soy::RectToVector( DestRect );
			
			//	normalise rects
			SourceRectv.x /= ImageTexture.GetWidth();
			SourceRectv.y /= ImageTexture.GetHeight();
			SourceRectv.z /= ImageTexture.GetWidth();
			SourceRectv.w /= ImageTexture.GetHeight();
			
			DestRectv.x /= Fbo->GetWidth();
			DestRectv.y /= Fbo->GetHeight();
			DestRectv.z /= Fbo->GetWidth();
			DestRectv.w /= Fbo->GetHeight();
			
			Shader.SetUniform("SrcImage",ImageTexture);
			Shader.SetUniform("MaskImage",MaskTexture);
			Shader.SetUniform("SrcRect",SourceRectv);
			Shader.SetUniform("DstRect",DestRectv);
			mBlitGeo->Draw();
			Fbo->Unbind();
		};
		auto& Semaphore = *Waits.PushBack( std::shared_ptr<Soy::TSemaphore>(new Soy::TSemaphore) );
		OpenglContext.PushJob( Blit, Semaphore );
		
		//	move on for next
		RectLeft = DestRect.Right() + Border.x;
		RowHeight = std::max( RowHeight, size_cast<size_t>(DestRect.h) );
	}

	//	wait for all blits to finish
	{
		Soy::TScopeTimerPrint BlitWaitTimer("MakeRectAtlas blit wait",10);
		for ( int i=0;	i<Waits.GetSize();	i++ )
		{
			auto& Wait = Waits[i];
			Wait->Wait();
		}
	}
	
	//	deffered delete of FBO
	Fbo->Delete( OpenglContext );
	
	return true;
}


void TFilterStageRuntimeData_MakeRectAtlas::Shutdown(Opengl::TContext& ContextGl,Opencl::TContext& ContextCl)
{
	auto DefferedDelete = [this]
	{
		mTexture.Delete();
	};
	
	Soy::TSemaphore Semaphore;
	ContextGl.PushJob( DefferedDelete, Semaphore );
	Semaphore.Wait();
}

