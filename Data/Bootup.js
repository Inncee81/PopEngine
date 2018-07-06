let VertShaderSource = `
	#version 410
	const vec4 Rect = vec4(0,0,1,1);
	in vec2 TexCoord;
	out vec2 uv;
	out float Blue_Frag;
	uniform float Blue;
	void main()
	{
		gl_Position = vec4(TexCoord.x,TexCoord.y,0,1);
		gl_Position.xy *= Rect.zw;
		gl_Position.xy += Rect.xy;
		//	move to view space 0..1 to -1..1
		gl_Position.xy *= vec2(2,2);
		gl_Position.xy -= vec2(1,1);
		uv = vec2(TexCoord.x,TexCoord.y);
		Blue_Frag = Blue;
	}
`;

let DebugFragShaderSource = `
	#version 410
	in vec2 uv;
	in float Blue_Frag;
	//out vec4 FragColor;
	void main()
	{
		gl_FragColor = vec4(uv.x,uv.y,Blue_Frag,1);
	}
`;

let ImageFragShaderSource = `
	#version 410
	in vec2 uv;
	uniform sampler2D Image;
	void main()
	{
		vec2 Flippeduv = vec2( uv.x, 1-uv.y );
		gl_FragColor = texture( Image, Flippeduv );
		//gl_FragColor *= vec4(uv.x,uv.y,0,1);
	}
`;

let DebugFrameFragShaderSource = `
	#version 410
	in vec2 uv;
	uniform sampler2D Image0;
	uniform sampler2D Image1;
	uniform sampler2D Image2;
	uniform sampler2D Image3;
	uniform sampler2D Image4;
	uniform sampler2D Image5;

	float Range(float Min,float Max,float Value)
	{
		return (Value-Min) / (Max-Min);
	}
	vec2 Range2(vec2 Min,vec2 Max,vec2 Value)
	{
		return vec2( Range(Min.x,Max.x,Value.x), Range(Min.y,Max.y,Value.y) );
	}

	void main()
	{
		vec2 Flippeduv = vec2( uv.x, 1-uv.y );
		float BoxsWide = 2;
		float BoxsHigh = 3;
		vec2 BoxUv = Range2( vec2(0.0,0.0), vec2( 1/BoxsWide, 1/BoxsHigh ), Flippeduv );
		float Indexf = floor(BoxUv.x) + ( floor(BoxUv.y) * BoxsWide );
		int Index = int(Indexf);
		
		gl_FragColor = vec4( fract( BoxUv ), 0, 1 );
		gl_FragColor = vec4( Flippeduv, 0, 1 );
		
		if ( Index == 0 )
			gl_FragColor = texture( Image0, fract( BoxUv ) );
		else if ( Index == 1 )
			gl_FragColor = texture( Image1, fract( BoxUv ) );
		else if ( Index == 2 )
			gl_FragColor = texture( Image2, fract( BoxUv ) );
		else if ( Index == 3 )
			gl_FragColor = texture( Image3, fract( BoxUv ) );
		else if ( Index == 4 )
			gl_FragColor = texture( Image4, fract( BoxUv ) );
		else if ( Index == 5 )
			gl_FragColor = texture( Image5, fract( BoxUv ) );

		//gl_FragColor *= vec4(uv.x,uv.y,0,1);
	}
`;

let RgbToHslFragShaderSource = LoadFileAsString('Data/FrameToHsl.frag');
let GrassFilterFragShaderSource = LoadFileAsString('Data/GrassFilter.frag');
let GrassLineFilterFragShaderSource = LoadFileAsString('Data/GrassLineFilter.frag');
let DrawLinesFragShaderSource = LoadFileAsString('Data/DrawLines.frag');
let TestLinesKernelSource = LoadFileAsString('Data/TestLines.cl');
let TestLinesKernelName = 'GetTestLines';
let HoughLinesKernelSource = LoadFileAsString('Data/HoughLines.cl');
let CalcAngleXDistanceXChunksKernelName = 'CalcAngleXDistanceXChunks';
let GraphAngleXDistancesKernelName = 'GraphAngleXDistances';


let EdgeFragShaderSource = `
	#version 410
	in vec2 uv;
	uniform sampler2D Image;

	float GetLum(vec3 rgb)
	{
		float lum = max( rgb.x, max( rgb.y, rgb.z ) );
		return lum;
	}

	float GetLumSample(vec2 uvoffset)
	{
		vec3 rgb = texture( Image, uv+uvoffset ).xyz;
		return GetLum(rgb);
	}

	void main()
	{
		vec2 ImageSize = vec2( 1280, 720 );
		vec2 uvstep2 = 1.0 / ImageSize;
		#define NeighbourCount	(3*3)
		float NeighbourLums[NeighbourCount];
		vec2 NeighbourSteps[NeighbourCount] =
		vec2[](
			vec2(-1,-1),	vec2(0,-1),	vec2(1,-1),
			vec2(-1,0),	vec2(0,0),	vec2(1,-1),
			vec2(-1,1),	vec2(0,1),	vec2(1,1)
		);
		
		for ( int n=0;	n<NeighbourCount;	n++ )
		{
			NeighbourLums[n] = GetLumSample( NeighbourSteps[n] * uvstep2 );
		}
		
		float BiggestDiff = 0;
		float ThisLum = NeighbourLums[4];
		for ( int n=0;	n<NeighbourCount;	n++ )
		{
			float Diff = abs( ThisLum - NeighbourLums[n] );
			BiggestDiff = max( Diff, BiggestDiff );
		}
		
		if ( BiggestDiff > 0.1 )
			gl_FragColor = vec4(1,1,1,1);
		else
			gl_FragColor = vec4(0,0,0,1);
	}
`;


var DrawImageShader = null;
var DebugShader = null;
var EdgeShader = null;
var DebugFrameShader = null;
var LastProcessedFrame = null;
var RgbToHslShader = null;
var GrassFilterShader = null;
var GrassLineFilterShader = null;
var DrawLinesShader = null;
var TestLinesKernel = null;
var CalcAngleXDistanceXChunksKernel = null;
var GraphAngleXDistancesKernel = null;

function GetRgbToHslShader(OpenglContext)
{
	if ( !RgbToHslShader )
	{
		RgbToHslShader = new OpenglShader( OpenglContext, VertShaderSource, RgbToHslFragShaderSource );
	}
	return RgbToHslShader;
}

function GetGrassFilterShader(OpenglContext)
{
	if ( !GrassFilterShader )
	{
		GrassFilterShader = new OpenglShader( OpenglContext, VertShaderSource, GrassFilterFragShaderSource );
	}
	return GrassFilterShader;
}

function GetGrassLineFilterShader(OpenglContext)
{
	if ( !GrassLineFilterShader )
	{
		GrassLineFilterShader = new OpenglShader( OpenglContext, VertShaderSource, GrassLineFilterFragShaderSource );
	}
	return GrassLineFilterShader;
}

function GetDrawLinesShader(OpenglContext)
{
	if ( !DrawLinesShader )
	{
		DrawLinesShader = new OpenglShader( OpenglContext, VertShaderSource, DrawLinesFragShaderSource );
	}
	return DrawLinesShader;
}

function GetTestLinesKernel(OpenclContext)
{
	if ( !TestLinesKernel )
	{
		TestLinesKernel = new OpenclKernel( OpenclContext, TestLinesKernelSource, TestLinesKernelName );
	}
	return TestLinesKernel;
}

function GetCalcAngleXDistanceXChunksKernel(OpenclContext)
{
	if ( !CalcAngleXDistanceXChunksKernel )
	{
		CalcAngleXDistanceXChunksKernel = new OpenclKernel( OpenclContext, HoughLinesKernelSource, CalcAngleXDistanceXChunksKernelName );
	}
	return CalcAngleXDistanceXChunksKernel;
}

function GetGraphAngleXDistancesKernel(OpenclContext)
{
	if ( !GraphAngleXDistancesKernel )
	{
		GraphAngleXDistancesKernel = new OpenclKernel( OpenclContext, HoughLinesKernelSource, GraphAngleXDistancesKernelName );
	}
	return GraphAngleXDistancesKernel;
}



function MakePromise(Func)
{
	return new Promise( Func );
}

function GetNumberRangeInclusive(Min,Max,Steps)
{
	let Numbers = [];
	for ( let t=0;	t<=1;	t+=1/Steps)
	{
		let v = Min + (t * (Max-Min));
		Numbers.push( v );
	}
	return Numbers;
}
	
function ReturnSomeString()
{
	return "Hello world";
}


function MakeHsl(OpenglContext,Frame)
{
	let Render = function(RenderTarget,RenderTargetTexture)
	{
		let Shader = GetRgbToHslShader(RenderTarget);

		let SetUniforms = function(Shader)
		{
			Shader.SetUniform("Frame", Frame, 0 );
		}
		
		RenderTarget.DrawQuad( Shader, SetUniforms );
	}
	
	Frame.Hsl = new Image( [Frame.GetWidth(),Frame.GetHeight() ] );
	let Prom = OpenglContext.Render( Frame.Hsl, Render );
	return Prom;
}


function MakeGrassMask(OpenglContext,Frame)
{
	let Render = function(RenderTarget,RenderTargetTexture)
	{
		let Shader = GetGrassFilterShader(RenderTarget);
		
		let SetUniforms = function(Shader)
		{
			Shader.SetUniform("Frame", Frame, 0 );
			Shader.SetUniform("hsl", Frame.Hsl, 1 );
		}
		
		RenderTarget.DrawQuad( Shader, SetUniforms );
	}
	
	let GrassMaskScale = 1/10;
	let GrassMaskWidth = Frame.GetWidth() * GrassMaskScale;
	let GrassMaskHeight = Frame.GetHeight() * GrassMaskScale;
	Frame.GrassMask = new Image( [GrassMaskWidth, GrassMaskHeight] );
	Frame.GrassMask.SetLinearFilter(true);
	let Prom = OpenglContext.Render( Frame.GrassMask, Render );
	return Prom;
}


function MakeLineMask(OpenglContext,Frame)
{
	let Render = function(RenderTarget,RenderTargetTexture)
	{
		let Shader = GetGrassLineFilterShader(RenderTarget);
		
		let SetUniforms = function(Shader)
		{
			Shader.SetUniform("Hsl", Frame.Hsl, 0 );
			Shader.SetUniform("GrassMask", Frame.GrassMask, 1 );
		}
		
		RenderTarget.DrawQuad( Shader, SetUniforms );
	}
	
	Frame.LineMask = new Image( [Frame.GetWidth(),Frame.GetHeight() ] );
	let Prom = OpenglContext.Render( Frame.LineMask, Render );
	return Prom;
}

function ExtractTestLines(Frame)
{
	let BoxLines = [
					[0,0,1,0],
					[0,1,1,1],
					[0,0,0,1],
					[1,0,1,1]
					];
	
	let Runner = function(Resolve,Reject)
	{
		if ( !Array.isArray(Frame.Lines) )
			Frame.Lines = new Array();
		Frame.Lines.push(...BoxLines);
		Resolve();
	}
	
	//	high level promise
	var Prom = new Promise( Runner );
	return Prom;
}

function ExtractOpenclTestLines(OpenclContext,Frame)
{
	Debug("Opencl ExtractLines");
	let Kernel = GetTestLinesKernel(OpenclContext);
	
	let OnIteration = function(Kernel,IterationIndexes)
	{
		//Debug("OnIteration(" + Kernel + ", " + IterationIndexes + ")");
		let LineBuffer = new Float32Array( 10*4 );
		let LineCount = new Int32Array(1);
		Kernel.SetUniform("Lines", LineBuffer );
		Kernel.SetUniform("LineCount", LineCount );
		Kernel.SetUniform("LinesSize", LineBuffer.length/4 );
	}
	
	let OnFinished = function(Kernel)
	{
		//Debug("OnFinished(" + Kernel + ")");
		let LineCount = Kernel.ReadUniform("LineCount");
		let Lines = Kernel.ReadUniform("Lines");
		if ( !Array.isArray(Frame.Lines) )
			Frame.Lines = new Array();
		Frame.Lines.push(...Lines);
		Debug("Output linecount=" + LineCount);
	}

	let Prom = OpenclContext.ExecuteKernel( Kernel, [1], OnIteration, OnFinished );
	return Prom;
}

function GraphAngleXDistances(OpenclContext,Frame)
{
	let Kernel = GetGraphAngleXDistancesKernel(OpenclContext);
	Frame.HoughHistogram = new Image( [1024,1024] );
	Debug(Frame.AngleXDistanceXChunks);

	let OnIteration = function(Kernel,IterationIndexes)
	{
		Debug("GraphAngleXDistances OnIteration(" + Kernel + ", " + IterationIndexes + ")");
		Kernel.SetUniform('xFirst', IterationIndexes[0] );
		Kernel.SetUniform('yFirst', IterationIndexes[1] );

		
		if ( IterationIndexes[0]==0 && IterationIndexes[1]==0 )
		{
			Kernel.SetUniform('AngleCount', Frame.Angles.length );
			Kernel.SetUniform('DistanceCount', Frame.Distances.length );
			Kernel.SetUniform('ChunkCount', Frame.ChunkCount );
			Kernel.SetUniform('HistogramHitMax', 5 );
			Kernel.SetUniform('AngleXDistanceXChunks', Frame.AngleXDistanceXChunks );
			Kernel.SetUniform('AngleXDistanceXChunkCount', Frame.AngleXDistanceXChunks.length );
			Kernel.SetUniform('GraphTexture', Frame.HoughHistogram );
		}
	}
	
	let OnFinished = function(Kernel)
	{
		Debug("GraphAngleXDistances OnFinished(" + Kernel + ")");
		Frame.HoughHistogram = Kernel.ReadUniform('GraphTexture');
		Debug(Frame.HoughHistogram.GetWidth());
		Debug("Done");
	}
	
	let Dim = [ Frame.HoughHistogram.GetWidth(), Frame.HoughHistogram.GetHeight() ];
	Debug("GraphAngleXDistances Dim=" + Dim);
	let Prom = OpenclContext.ExecuteKernel( Kernel, Dim, OnIteration, OnFinished );
	return Prom;
}

function CalcAngleXDistanceXChunks(OpenclContext,Frame)
{
	let Kernel = GetCalcAngleXDistanceXChunksKernel(OpenclContext);
	let MaskTexture = Frame.LineMask;
	Frame.Angles = GetNumberRangeInclusive( 0, 179, 179/2 );
	Frame.Distances = GetNumberRangeInclusive( -1, 1, 100 );
	Frame.ChunkCount = 10;
	Frame.AngleXDistanceXChunks = new Uint32Array( Frame.Angles.length * Frame.Distances.length * Frame.ChunkCount );
	
	let OnIteration = function(Kernel,IterationIndexes)
	{
		Debug("CalcAngleXDistanceXChunks OnIteration(" + Kernel + ", " + IterationIndexes + ")");
		Kernel.SetUniform('xFirst', IterationIndexes[0] );
		Kernel.SetUniform('yFirst', IterationIndexes[1] );
		Kernel.SetUniform('AngleIndexFirst', IterationIndexes[2] );
		Kernel.SetUniform('Angles', Frame.Angles );
		Kernel.SetUniform('Distances', Frame.Distances );
		Kernel.SetUniform('DistanceCount', Frame.Distances.length );
		Kernel.SetUniform('ChunkCount', Frame.ChunkCount );
		if ( IterationIndexes[0]==IterationIndexes[1]==IterationIndexes[2]==0 )
		{
			Kernel.SetUniform('EdgeTexture', MaskTexture );
			Kernel.SetUniform('AngleXDistanceXChunks', Frame.AngleXDistanceXChunks );
			Kernel.SetUniform('AngleXDistanceXChunkCount', Frame.AngleXDistanceXChunks.length );
		}
	}
	
	let OnFinished = function(Kernel)
	{
		Debug("CalcAngleXDistanceXChunks OnFinished(" + Kernel + ")");
		Frame.AngleXDistanceXChunks = Kernel.ReadUniform('AngleXDistanceXChunks');
	}

	let Dim = [MaskTexture.GetWidth(),MaskTexture.GetHeight(), Frame.Angles.length];
	Debug("CalcAngleXDistanceXChunks Dim=" + Dim);
	let Prom = OpenclContext.ExecuteKernel( Kernel, Dim, OnIteration, OnFinished );
	return Prom;
}

function ExtractHoughLines(OpenclContext,Frame)
{
	let HoughRunner = function(Resolve,Reject)
	{
		let a = function()	{	return CalcAngleXDistanceXChunks(OpenclContext,Frame);	}
		let b = function()	{	return GraphAngleXDistances(OpenclContext,Frame);	}
		let c = function()	{	Debug("ExtractHoughLines");	}
		let OnError = function(err)
		{
			Debug("hough runner error: " + err);
			Reject();
		};
		
		a()
		.then( b )
		//.then( MakePromise(c) )
		.then( MakePromise(Resolve) )
		.catch( OnError );
	}
	
	//	high level promise
	return MakePromise( HoughRunner );
}

function DrawLines(OpenglContext,Frame)
{
	Debug("DrawLines");
	let Render = function(RenderTarget,RenderTargetTexture)
	{
		let Shader = GetDrawLinesShader(RenderTarget);
		
		let SetUniforms = function(Shader)
		{
			if ( !Array.isArray(Frame.Lines) )
				Frame.Lines = [];
			Shader.SetUniform("Lines", Frame.Lines );
			//	gr: causing uniform error
			//Shader.SetUniform("Background", Frame, 0 );
		}
		
		RenderTarget.DrawQuad( Shader, SetUniforms );
	}
	
	Frame.DebugLines = new Image( [Frame.GetWidth(),Frame.GetHeight() ] );
	let Prom = OpenglContext.Render( Frame.DebugLines, Render );
	return Prom;
}


function StartProcessFrame(Frame,OpenglContext,OpenclContext)
{
	Debug( "Frame size: " + Frame.GetWidth() + "x" + Frame.GetHeight() );
	//LastProcessedFrame = Frame;
	
	let OnError = function(Error)
	{
		Debug(Error);
	};
	
	let Part1 = function()	{	return MakeHsl( OpenglContext, Frame );	}
	let Part2 = function()	{	return MakeGrassMask( OpenglContext, Frame );	}
	let Part3 = function()	{	return MakeLineMask( OpenglContext, Frame );	}
	let Part4 = function()	{	return ExtractTestLines( Frame );	}
	let Part5 = function()	{	return ExtractOpenclTestLines( OpenclContext, Frame );	}
	let Part6 = function()	{	return ExtractHoughLines( OpenclContext, Frame );	}
	let Part7 = function()	{	return DrawLines( OpenglContext, Frame );	}
	let Finish = function()
	{
		LastProcessedFrame = Frame;
		Debug("Done!");
	};
	
	//	run sequence
	Part1()
	.then( Part2 )
	.then( Part3 )
	.then( Part4 )
	.then( Part5 )
	.then( Part6 )
	.then( Part7 )
	.then( Finish )
	.catch( OnError );
}


function WindowRender(RenderTarget)
{
	try
	{
		if ( LastProcessedFrame == null )
		{
			RenderTarget.ClearColour(0,1,1);
			return;
		}
		
		if ( !DebugFrameShader )
		{
			DebugFrameShader = new OpenglShader( RenderTarget, VertShaderSource, DebugFrameFragShaderSource );
		}
		
		let SetUniforms = function(Shader)
		{
			//Blue = (Blue==0) ? 1 : 0;
			//Debug("On bind: " + Blue);
			//Shader.SetUniform("Blue", Blue );
			//Debug( typeof(Pitch) );
			Shader.SetUniform("Image0", LastProcessedFrame, 0 );
			Shader.SetUniform("Image1", LastProcessedFrame.Hsl, 1 );
			Shader.SetUniform("Image2", LastProcessedFrame.GrassMask, 2 );
			Shader.SetUniform("Image3", LastProcessedFrame.LineMask, 3 );
			Shader.SetUniform("Image4", LastProcessedFrame.DebugLines, 4 );
			Shader.SetUniform("Image5", LastProcessedFrame.HoughHistogram, 5 );
		}
		
		RenderTarget.ClearColour(0,1,0);
		RenderTarget.DrawQuad( DebugFrameShader, SetUniforms );
	}
	catch(Exception)
	{
		RenderTarget.ClearColour(1,0,0);
		Debug(Exception);
	}
}

function Main()
{
	//Debug("log is working!", "2nd param");
	let Window1 = new OpenglWindow("Hello!");
	Window1.OnRender = function(){	WindowRender( Window1 );	};
	
	
	let OpenclDevices = OpenclEnumDevices();
	Debug("Opencl devices x" + OpenclDevices.length );
	if ( OpenclDevices.length == 0 )
		throw "No opencl devices";
	OpenclDevices.forEach( Debug );
	let Opencl = new OpenclContext( OpenclDevices[0] );
	
	let Pitch = new Image("Data/ArgentinaVsCroatia.png");
	//let Pitch = new Image("Data/Cat.jpg");
	
	let OpenglContext = Window1;
	StartProcessFrame( Pitch, OpenglContext, Opencl );
}

//	main
Main();