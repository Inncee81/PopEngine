#pragma once

#include <SoyProtocol.h>
#include <SoyHttp.h>


namespace WebSocket
{
	class TRequestProtocol;
	class THandshakeMeta;
	class THandshakeResponseProtocol;
	class TMessageHeader;
	class TMessage;
	
	namespace TOpCode
	{
		enum Type
		{
			Invalid					= -1,
			ContinuationFrame		= 0,
			TextFrame				= 1,
			BinaryFrame				= 2,
			ConnectionCloseFrame	= 8,
			PingFrame				= 9,
			PongFrame				= 10,
		};
		DECLARE_SOYENUM( WebSocket::TOpCode );
	}

}



//	gr: name is a little misleading, it's the websocket connection meta
class WebSocket::THandshakeMeta
{
public:
	THandshakeMeta();
	
	std::string			GetReplyKey() const;
	bool				IsCompleted() const	{	return mIsWebSocketUpgrade && mWebSocketKey.length()!=0 && mVersion.length()!=0;	}
	
public:
	//	protocol and version are optional
	std::string			mProtocol;
	std::string			mVersion;
	bool				mIsWebSocketUpgrade;
	std::string			mWebSocketKey;
};


class WebSocket::TMessage
{
public:
	TMessage() :
		mIsComplete	(false )
	{
	}
	
	void				PushMessageData(TOpCode::Type PayloadFormat,bool IsLastPayload,const ArrayBridge<char>&& Payload);
	void				PushTextMessageData(const ArrayBridge<char>& Payload,bool IsLastPayload);
	void				PushBinaryMessageData(const ArrayBridge<char>& Payload,bool IsLastPayload);
	
	bool				IsCompleteTextMessage() const	{	return mIsComplete && mTextData.length() > 0;	}
	bool				IsCompleteBinaryMessage() const	{	return mIsComplete && mBinaryData.GetSize() > 0;	}
	
	
public:
	bool				mIsComplete;
	Array<uint8_t>		mBinaryData;	//	binary message
	std::string			mTextData;		//	text message
};

class WebSocket::TMessageHeader
{
public:
	TMessageHeader() :
		Length		( 0 ),
		Length16	( 0 ),
		LenMostSignificant	( 0 ),
		Length64	( 0 ),
		Fin			( 1 ),
		Reserved	( 0 ),
		OpCode		( WebSocket::TOpCode::Invalid ),
		Masked		( false )
	{
	}
	
	int		Fin;
	int		Reserved;
	int		OpCode;
	int		Masked;
	int		Length;
	int		Length16;
	int		LenMostSignificant;
	uint64	Length64;
	BufferArray<unsigned char,4> MaskKey;	//	store & 32 bit int
	
	TOpCode::Type	GetOpCode() const	{	return TOpCode::Validate( OpCode );	}
	bool			IsText() const			{	return OpCode == TOpCode::TextFrame;	}
	size_t			GetLength() const;
	std::string		GetMaskKeyString() const;
	void			IsValid() const;					//	throws if not valid
	bool			Decode(TStreamBuffer& Data);		//	returns false if not got enough data. throws on error
	bool			Encode(ArrayBridge<char>& Data,const ArrayBridge<char>& MessageData,std::stringstream& Error);
};



//	a websocket client
class WebSocket::TRequestProtocol : public Http::TRequestProtocol
{
public:
	TRequestProtocol() : mHandshake(* new THandshakeMeta() ), mMessage( *new TMessage() )	{	throw Soy::AssertException("Should not be called");	}
	TRequestProtocol(THandshakeMeta& Handshake,TMessage& Message) :
		mHandshake	( Handshake ),
		mMessage	( Message )
	{
	}

	virtual TProtocolState::Type	Decode(TStreamBuffer& Buffer) override;
	virtual bool					ParseSpecificHeader(const std::string& Key,const std::string& Value) override;
	
protected:
	TProtocolState::Type	DecodeBody(TMessageHeader& Header,TStreamBuffer& Buffer);

public:
	//	if we've generated a "reply with this message" its the HTTP-reply to allow websocket (and not an actual packet)
	//	if this is present, send it to the socket :)
	std::shared_ptr<Soy::TWriteProtocol>	mReplyMessage;
	
	THandshakeMeta&		mHandshake;	//	persistent handshake data etc
	TMessage&			mMessage;	//	persistent message for multi-frame messages
};


class WebSocket::THandshakeResponseProtocol : public Http::TResponseProtocol
{
public:
	THandshakeResponseProtocol(const THandshakeMeta& Handshake);
};



