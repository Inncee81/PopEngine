#define const	__constant

const int SampleRadius = 8;	//	range 0,9
const int HitCountMin = 1;
const bool IncludeSelf = true;



static float4 texture2D(__read_only image2d_t Image,int2 uv,int2 PixelOffset)
{
	sampler_t Sampler = CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;
	
	return read_imagef( Image, Sampler, uv + PixelOffset );
}



__kernel void AlphaFill(int OffsetX,int OffsetY,__read_only image2d_t grassfilter,__write_only image2d_t Frag)
{
	float x = get_global_id(0) + OffsetX;
	float y = get_global_id(1) + OffsetY;
	int2 uv = (int2)( x, y );

	float4 ThisSample = texture2D( grassfilter, uv, (int2)(0,0) );
	int HitCount = 0;
	
	for ( int y=-SampleRadius;	y<=SampleRadius;	y++ )
	{
		for ( int x=-SampleRadius;	x<=SampleRadius;	x++ )
		{
			if ( !IncludeSelf && y==0 && x==0 )
				continue;

			float4 NeighbourSample = texture2D( grassfilter, uv, (int2)(x,y) );
			bool NeighbourHit = (NeighbourSample.w > 0.5f);
			HitCount += NeighbourHit ? 1 : 0;
		}
	}

	if ( HitCount > HitCountMin )
	{
		ThisSample.xyz = (float3)(0,1,0);
	}
	else
	{
		ThisSample.xyz = (float3)(1,0,0);
	}
//	ThisSample.xyz = (float3)(1,0,0);
	//	ThisSample.w = (HitCount > HitCountMin) ? 1 : 0;
	ThisSample.w = 1;
	write_imagef( Frag, uv, ThisSample );
}
