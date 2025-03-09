//credit: https://www.gamedev.net/tutorials/programming/graphics/fur-shading-in-directx-9-r5348/

#include "tAutodeskMemoryStream.h"

FbxStream::EState tAutodeskMemoryStream::GetState()
{
   return _state;
}

bool tAutodeskMemoryStream::Open(void* pStreamData)
{
   _state = EState::eOpen;
   _position = 0;
   return true;
}

bool tAutodeskMemoryStream::Close()
{
   _state = EState::eClosed;
   _position = 0;
   return true;
}

bool tAutodeskMemoryStream::Flush()
{
   return true;
}

size_t tAutodeskMemoryStream::Write(const void*, FbxUInt64)
{
   _errorCode = 1;
   return 0;
}

size_t tAutodeskMemoryStream::Read(void* buffer, FbxUInt64 count) const
{
   long remain = _length - _position;
   if (count > remain)
   {
      const_cast<tAutodeskMemoryStream&>(*this)._errorCode = 1;
      return 0;
   }
   memcpy_s(buffer, count, _stream + _position, count);
   const_cast<tAutodeskMemoryStream&>(*this)._position += count;
   return count;
}

int tAutodeskMemoryStream::GetReaderID() const
{
   return _readerId;
}

int tAutodeskMemoryStream::GetWriterID() const
{
   return -1;
}

void tAutodeskMemoryStream::Seek(const FbxInt64& pOffset, const FbxFile::ESeekPos& pSeekPos)
{
   long offset = static_cast<long>(pOffset);
   switch (pSeekPos)
   {
   case FbxFile::ESeekPos::eCurrent:
      _position += offset;
      break;
   case FbxFile::ESeekPos::eEnd:
      _position = _length - offset;
      break;
   default:
      _position = offset;
      break;
   }
}

FbxInt64 tAutodeskMemoryStream::GetPosition() const
{
   return _position;
}

void tAutodeskMemoryStream::SetPosition(FbxInt64 pPosition)
{
   _position = pPosition;
}

int tAutodeskMemoryStream::GetError() const
{
   return _errorCode;
}

void tAutodeskMemoryStream::ClearError()
{
   _errorCode = 0;
}
