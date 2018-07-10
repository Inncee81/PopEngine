#version 410
in vec2 uv;
uniform sampler2D Image0;
uniform sampler2D Image1;
uniform sampler2D Image2;
uniform sampler2D Image3;
uniform sampler2D Image4;
uniform sampler2D Image5;
uniform sampler2D Image6;

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
	float BoxsWide = 3;
	float BoxsHigh = 3;
	
	BoxsWide = 2;	BoxsHigh = 1;
	
	//	debug just one section
	//BoxsWide = BoxsHigh = 1;

	int ImageOrder[9];
	ImageOrder[0] = 5;
	ImageOrder[1] = 6;
	ImageOrder[2] = 0;
	ImageOrder[3] = 1;
	ImageOrder[4] = 2;
	ImageOrder[5] = 3;
	ImageOrder[6] = 4;

	vec2 BoxUv = Range2( vec2(0.0,0.0), vec2( 1/BoxsWide, 1/BoxsHigh ), Flippeduv );
	float Indexf = floor(BoxUv.x) + ( floor(BoxUv.y) * BoxsWide );
	int Index = int(Indexf);
	
	Index = ImageOrder[Index];
	
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
	else if ( Index == 6 )
		gl_FragColor = texture( Image6, fract( BoxUv ) );
	
	//gl_FragColor *= vec4(uv.x,uv.y,0,1);
}