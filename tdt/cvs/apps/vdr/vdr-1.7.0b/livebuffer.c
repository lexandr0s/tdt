#ifdef USE_LIVEBUFFER
#include "livebuffer.h"
#include "menu.h"
#include "recording.h"
#include "transfer.h"

#define RECORDERBUFSIZE  MEGABYTE(2)

// --- cFileName -------------------------------------------------------------

#include <errno.h>
#include <unistd.h>
#include "videodir.h"

#define MAXFILESPERRECORDING 255
#define RECORDFILESUFFIX    "/%03d.vdr"
#define RECORDFILESUFFIXLEN 20 

cFileName64::cFileName64(const char *FileName, bool Record, bool Blocking)
{
  file = NULL;
  record = Record;
  blocking = Blocking;
  // Prepare the file name:
  fileName = MALLOC(char, strlen(FileName) + RECORDFILESUFFIXLEN);
  if (!fileName) {
     esyslog("ERROR: can't copy file name '%s'", fileName);
     return;
     }
  strcpy(fileName, FileName);
  pFileNumber = fileName + strlen(fileName);

  fileNumber = 1;
  while (true) {
    sprintf(pFileNumber, RECORDFILESUFFIX, fileNumber);
    if (record) {
       if (access(fileName, F_OK) == 0) {
          struct stat64 buf;
          if (stat64(fileName, &buf) == 0) {
             fileNumber++;
             continue;
             }
          }
       else if (errno != ENOENT) 
          LOG_ERROR_STR(fileName);
       }
    break;
    }
}

cFileName64::~cFileName64()
{
  Close();
  free(fileName);
}

void cFileName64::SetNumber(int Number)
{
  fileNumber = Number;
  sprintf(pFileNumber, RECORDFILESUFFIX, fileNumber);
}

cUnbufferedFile64 *cFileName64::Open(void)
{
  if (!file) {
     int BlockingFlag = blocking ? 0 : O_NONBLOCK;
     if (record) {
        file = cUnbufferedFile64::Create(fileName, O_RDWR | O_CREAT | BlockingFlag);
        if (!file)
           LOG_ERROR_STR(fileName);
        }
     else {
        if (access(fileName, R_OK) == 0) {
           file = cUnbufferedFile64::Create(fileName, O_RDONLY | BlockingFlag);
           if (!file)
              LOG_ERROR_STR(fileName);
           }
        else if (errno != ENOENT)
           LOG_ERROR_STR(fileName);
        }
     }
  return file;
}

void cFileName64::Close(void)
{
  if (file) {
     if (file->Close() < 0)
        LOG_ERROR_STR(fileName);
     delete file;
     file = NULL;
     }
}

void cFileName64::CloseAndRemove(void)
{
  Close();
  remove(fileName);
}

// --- cUnbufferedFile64 -----------------------------------------------------

#define USE_FADVISE

#define WRITE_BUFFER KILOBYTE(800)

cUnbufferedFile64::cUnbufferedFile64(void)
{
  fd = -1;
}

cUnbufferedFile64::~cUnbufferedFile64()
{
  Close();
}

int cUnbufferedFile64::Open(const char *FileName, int Flags, mode_t Mode)
{
  Close();
  fd = open64(FileName, Flags, Mode);
  curpos = 0;
#ifdef USE_FADVISE
  begin = lastpos = ahead = 0;
  cachedstart = 0;
  cachedend = 0;
  readahead = KILOBYTE(128);
  written = 0;
  totwritten = 0;
  if (fd >= 0)
     posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM); 
#endif
  return fd;
}

int cUnbufferedFile64::Close(void)
{
#ifdef USE_FADVISE
  if (fd >= 0) {
     if (totwritten)    
        fdatasync(fd);  
     posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
     }
#endif
  int OldFd = fd;
  fd = -1;
  return close(OldFd);
}


#define FADVGRAN   KILOBYTE(4) 
#define READCHUNK  MEGABYTE(8)

int cUnbufferedFile64::FadviseDrop(off64_t Offset, off64_t Len)
{
  return posix_fadvise64(fd, Offset - (FADVGRAN - 1), Len + (FADVGRAN - 1) * 2, POSIX_FADV_DONTNEED);
}


off64_t cUnbufferedFile64::Seek(off64_t Offset, int Whence)
{
  if (Whence == SEEK_SET && Offset == curpos)
     return curpos;
  curpos = lseek64(fd, Offset, Whence);
  return curpos;
}

ssize_t cUnbufferedFile64::Read(void *Data, size_t Size)
{
  if (fd >= 0) {
#ifdef USE_FADVISE
     off64_t jumped = curpos-lastpos; 
     if ((cachedstart < cachedend) && (curpos < cachedstart || curpos > cachedend)) {
        FadviseDrop(cachedstart, cachedend-cachedstart);
        cachedstart = curpos;
        cachedend = curpos;
        }
     cachedstart = min(cachedstart, curpos);
#endif
     ssize_t bytesRead = safe_read(fd, Data, Size);
#ifdef USE_FADVISE
     if (bytesRead > 0) {
        curpos += bytesRead;
        cachedend = max(cachedend, curpos);

        if (jumped >= 0 && jumped <= (off64_t)readahead) {
           if (ahead - curpos < (off64_t)(readahead / 2)) {
              posix_fadvise64(fd, curpos, readahead, POSIX_FADV_WILLNEED);
              ahead = curpos + readahead;
              cachedend = max(cachedend, ahead);
              }
           if (readahead < Size * 32) { 
              readahead = Size * 32;
              }
           }
        else
           ahead = curpos; 
        }

     if (cachedstart < cachedend) {
        if (curpos - cachedstart > READCHUNK * 2) {
           FadviseDrop(cachedstart, curpos - READCHUNK - cachedstart);
           cachedstart = curpos - READCHUNK;
           }
        else if (cachedend > ahead && cachedend - curpos > READCHUNK * 2) {
           FadviseDrop(curpos + READCHUNK, cachedend - (curpos + READCHUNK));
           cachedend = curpos + READCHUNK;
           }
        }
     lastpos = curpos;
#endif
     return bytesRead;
     }
  return -1;
}

ssize_t cUnbufferedFile64::Write(const void *Data, size_t Size)
{
  if (fd >=0) {
     ssize_t bytesWritten = safe_write(fd, Data, Size);
#ifdef USE_FADVISE
     if (bytesWritten > 0) {
        begin = min(begin, curpos);
        curpos += bytesWritten;
        written += bytesWritten;
        lastpos = max(lastpos, curpos);
        if (written > WRITE_BUFFER) {
           if (lastpos > begin) {
              off64_t headdrop = min(begin, (off64_t) WRITE_BUFFER * 2L);
              posix_fadvise64(fd, begin - headdrop, lastpos - begin + headdrop, POSIX_FADV_DONTNEED);
              }
           begin = lastpos = curpos;
           totwritten += written;
           written = 0;
           if (totwritten > MEGABYTE(32)) {
              off64_t headdrop = min((off64_t) (curpos - totwritten), (off64_t) totwritten * 2L);
              posix_fadvise64(fd, curpos - totwritten - headdrop, totwritten + headdrop, POSIX_FADV_DONTNEED);
              totwritten = 0;
              }
           }
        }
#endif
     return bytesWritten;
     }
  return -1;
}

cUnbufferedFile64 *cUnbufferedFile64::Create(const char *FileName, int Flags, mode_t Mode)
{
  cUnbufferedFile64 *File = new cUnbufferedFile64;
  if (File->Open(FileName, Flags, Mode) < 0) {
     delete File;
     File = NULL;
     }
  return File;
}

// --- cLiveIndex ------------------------------------------------------------

cLiveIndex::cLiveIndex(int BufSize)
{
  bufSize = BufSize;
  headblock = tailblock = new block;
  headblock->previous = headblock->next = headblock;
  head = tail = 0;
  blank = 0;
  blockCount = 1;
  writtenCount = delCount = 0;
  lastWrittenSize = lastCount = 0;
  fileSize = 0;
  lastFrame = -1;
  DPos = -1;
  DCount = 0;
  start = 0;
  RwLock = new cRwLock(true);
}

cLiveIndex::~cLiveIndex()
{
  while (tailblock != headblock) {
    block *n = tailblock->next;
    delete tailblock;
    tailblock = n;
    } 
  delete headblock;
  delete RwLock;
}

void cLiveIndex::Add(uchar pt, int pos)
{
  RwLock->Lock(true);
  DCount=0;
  DPos = pos;
  headblock->Frame[head].offset = pos;
  headblock->Frame[head].written = false;
  headblock->Frame[head].pt = pt;
  head++;
  lastFrame++;
  if (head == INDEXBLOCKSIZE) {
    block* b = new block;
    b->previous = headblock;
    b->next = b;
    headblock->next = b;
    headblock = b;
    head = 0;
    blockCount++; 
    }
  RwLock->Unlock();
}

void cLiveIndex::AddData(int pos)
{
  RwLock->Lock(true);
  DCount=pos-DPos; 
  RwLock->Unlock();
}

void cLiveIndex::Delete(off64_t FilePos)
{
  RwLock->Lock(true);
  if (tailblock->Frame[tail].offset <= FilePos) {
    if (tailblock->Frame[tail].offset + MAXFRAMESIZE >= FilePos) {
      int old_offset = tailblock->Frame[tail].offset;
      while (tailblock->Frame[tail].written && tailblock->Frame[tail].offset < FilePos && tailblock->Frame[tail].offset >= old_offset) {
        tail++;
        delCount++;
        if (tail == INDEXBLOCKSIZE) {
          block *b = tailblock->next;
          b->previous = b;
          if (tailblock != headblock)
            delete tailblock;
          tailblock = b;
          tail = 0;
          blockCount--;
          }
        }
      }
    }
  else {
    if (tailblock->Frame[tail].offset - MAXFRAMESIZE > FilePos) {
      int old_offset = tailblock->Frame[tail].offset;
      while (tailblock->Frame[tail].written && (tailblock->Frame[tail].offset < FilePos || tailblock->Frame[tail].offset >= old_offset)) {
        tail++;
        delCount++;
        if (tail == INDEXBLOCKSIZE) {
          block *b = tailblock->next;
          b->previous = b;
          if (tailblock != headblock)
            delete tailblock;
          tailblock = b;
          tail = 0;
          blockCount--;
          }
        }
      }
    }
  RwLock->Unlock();
}

int cLiveIndex::DelFirst(void)
{
  RwLock->Lock(true);
  tail++;
  delCount++;
  writtenCount++;
  if (tail == INDEXBLOCKSIZE) {
     block *b = tailblock->next;
     b->previous = b;
     if (tailblock != headblock)
        delete tailblock;
     tailblock = b;
     tail = 0;
     blockCount--;
     }
  int r = tailblock->Frame[tail].offset;
  RwLock->Unlock();
  return r;
}

void cLiveIndex::Clear(void)
{
  RwLock->Lock(true);
  while (tailblock != headblock) {
    block *n = tailblock->next;
    delete tailblock;
    tailblock = n;
    }
  headblock->previous = headblock;
  head = tail = blank = 0;
  blockCount = 1;
  writtenCount = delCount = 0;
  DPos = -1;
  DCount = 0;
  start = 0;
  lastFrame = -1;
  lastWrittenSize = lastCount = 0;
  fileSize = 0; 
  RwLock->Unlock();
}

int cLiveIndex::NextWrite(void)
{
  RwLock->Lock(false);
  block *b;
  int i = GetFrame(&b,writtenCount);
  int off = 0;
  if (i+1 == INDEXBLOCKSIZE)
    off = b->next->Frame[0].offset; 
  else 
    off = b->Frame[i+1].offset;
  int r = 0;
  if (off < b->Frame[i].offset)
     r = bufSize - b->Frame[i].offset - blank;
  else
    r = off - b->Frame[i].offset;
  RwLock->Unlock();
  return r;
}

void cLiveIndex::WrittenTo(off64_t FilePos, int Count)
{
  RwLock->Lock(true);
  lastWrittenSize=Count;
  block *b;
  int i = GetFrame(&b, writtenCount);
  writtenCount++;
  if (FilePos == 0) {
    if (i)
      fileSize = b->Frame[i-1].offset + lastCount;
    else if (b->previous != b)
      fileSize = b->previous->Frame[INDEXBLOCKSIZE-1].offset + lastCount;
    }
  b->Frame[i].offset = FilePos;
  b->Frame[i].written = true;
  RwLock->Unlock();
  lastCount = Count;
}

int cLiveIndex::GetFrame(block** Block, int Number)
{
  int bc = 0;
  int i = Number-delCount;
  if (INDEXBLOCKSIZE-tail <= i)
    bc = 1 + (i-INDEXBLOCKSIZE+tail)/INDEXBLOCKSIZE;
  i-= bc*INDEXBLOCKSIZE-tail;
  block *b = tailblock;
  for (int j=0; j<bc; j++)
    b = b->next;
  *Block = b;
  return i;
}

off64_t cLiveIndex::GetOffset(int Number)
{
  RwLock->Lock(false);
  off64_t r = -1;
  if (HasFrame(Number)) {
    block *b;
    int i = GetFrame(&b, Number);
    r = b->Frame[i].offset;
    }
  else if (HasFrame(Number-1))
    r = DPos;
  RwLock->Unlock();
  return r;
}

int cLiveIndex::Size(int Number, uchar *PictureType)
{
  int r = -1;
  RwLock->Lock(false);
  if (HasFrame(Number)) {
    if (Number+1 == writtenCount)
      r = lastWrittenSize;
    else {
      block *b;
      int i = GetFrame(&b, Number);
      if (PictureType)
        *PictureType = b->Frame[i].pt;
      off64_t off = 0;
      if (i+1 == INDEXBLOCKSIZE)
        off = b->next->Frame[0].offset;
      else
        off = b->Frame[i+1].offset;
      if (off < b->Frame[i].offset) {
        if (b->Frame[i].written)
          r = fileSize - b->Frame[i].offset;
        else
          r = bufSize - b->Frame[i].offset - blank;
        }
      else
        r = off - b->Frame[i].offset;    
      }
    }
  else if (HasFrame(Number-1) && DPos >= 0)
    r = DCount;
  RwLock->Unlock();
  return r;
}

int cLiveIndex::GetNextIFrame(int Index, bool Forward)
{
  int d = Forward ? 1 : -1;
  Index += d;
  while (Index >= delCount && Index < lastFrame) {
    block *b;
    RwLock->Lock(false);
    int i = GetFrame(&b, Index);
    uchar type = b->Frame[i].pt;
    RwLock->Unlock();
    if (type == I_FRAME)
       return Index;
    Index +=d;
    }
  return -1; 
}

int cLiveIndex::FindIFrame(int64_t PTS, uchar *Buffer)
{
  int r = -1;
  RwLock->Lock(false);
  int Index=writtenCount;
  int64_t pts=0;
  do {
    Index = GetNextIFrame(Index, true);
    if (Index < 0)
      break;
    block *b;
    int i = GetFrame(&b, Index);
    uchar *p = &Buffer[b->Frame[i].offset];
    if (p[0]==0x00 && p[1]==0x00 && p[2]==0x01 && (p[7] & 0x80) && p[3]>=0xC0 && p[3]<=0xEF) {
       pts  = (int64_t) (p[ 9] & 0x0E) << 29 ;
       pts |= (int64_t)  p[ 10]         << 22 ;
       pts |= (int64_t) (p[ 11] & 0xFE) << 14 ;
       pts |= (int64_t)  p[ 12]         <<  7 ;
       pts |= (int64_t) (p[ 13] & 0xFE) >>  1 ;
      }
  } while (pts!=PTS);
  if (pts==PTS)
    r = Index;
  RwLock->Unlock();
  return r;
}

void cLiveIndex::Switched(void)
{
  start = Last();
}

// --- cLiveFileReader -------------------------------------------------------

cLiveFileReader::cLiveFileReader(const char *FileName, int Number)
{
  fileName = new cFileName64(FileName, false);
  fileName->SetNumber(Number);
  readFile = fileName->Open();
  buffer = NULL;
  filePos = 0;
  Start();
}

cLiveFileReader::~cLiveFileReader()
{
  Cancel(3);
  delete fileName;
}

void cLiveFileReader::Action(void)
{
  while (Running()) {
    Lock();
    if (buffer && !hasData) {
      int r = readFile->Read(buffer + length, wanted - length);
      if (r >= 0) {
        length += r;
        filePos += r;
        if (length == wanted)
          hasData = true;
        }
      else if (r < 0 && FATALERRNO) {
        LOG_ERROR;
        length = r;
        hasData = true;
        }
      } 
    Unlock();
    newSet.Wait(1000);   
    }
}

int cLiveFileReader::Read(uchar **Buffer, off64_t FilePos, int Size)
{
  if (buffer && hasData) {
    *Buffer = buffer;
    buffer = NULL;
    return length;
    }
  if (!buffer) {
    uchar *b = MALLOC(uchar, Size);
    if (filePos != FilePos) {
      filePos = FilePos;
      readFile->Seek(FilePos,0);
      }
    wanted = Size;
    length = 0;
    hasData = false;
    buffer = b;
    newSet.Signal();
    }
  return -1;
}

void cLiveFileReader::Clear(void)
{
  Lock();
  delete buffer;
  buffer = NULL; 
  Unlock();
}

// --- cLiveFileWriter -------------------------------------------------------

cLiveFileWriter::cLiveFileWriter(const char *FileName)
{
  MakeDirs(FileName, true);
  SpinUpDisk(FileName);
  fileName = new cFileName64(FileName, true);
  writeFile = fileName->Open();
  buffer = NULL;
  filePos = fileSize = 0;
  lastCheck = time(NULL);
  full = false;
  Start();
}

cLiveFileWriter::~cLiveFileWriter()
{
  Cancel(3);
  fileName->CloseAndRemove();
  delete fileName;
}

void cLiveFileWriter::Action(void)
{
  while (Running()) {
    Lock();
    if (buffer && !hasWritten) {
      int r = writeFile->Write(buffer, wantWrite);
      if (r >= 0) {
        filePos += r;
        hasWritten = true;
        }
      else if (r < 0 && FATALERRNO) {
        LOG_ERROR;
        hasWritten = true;
        }
      }
    Unlock();
    newSet.Wait(1000);
    }
}

#define MINFREE 600

bool cLiveFileWriter::LowDiskSpace()
{
  if (!full && time (NULL) > lastCheck + 60 || full && filePos >= fileSize) {
    int Free = FreeDiskSpaceMB(fileName->Name());
    lastCheck = time(NULL);
    if (Free < MINFREE) 
      return true;
    }
  return false;
}

off64_t cLiveFileWriter::Write(uchar *Buffer, int Size, bool Wait)
{
  if (buffer && hasWritten) {
    buffer = NULL;
    return filePos;
    }
  if (!buffer) {
    if (filePos + Size > (off64_t)Setup.LiveBufferSize*1024*1024 || LowDiskSpace()) { 
      fileSize = filePos > fileSize ? filePos : fileSize;
      filePos = 0;
      writeFile->Seek(0,0);
      full = true;
      }
    if (Wait) {
      writeFile->Write(Buffer, Size);
      filePos += Size;
      return filePos;
      }
    else {
      wantWrite = Size;
      hasWritten = false;
      buffer = Buffer;
      newSet.Signal();
      }
    }
  if (Wait) {
    while (!hasWritten)
       usleep(5);
    buffer = NULL;
    return filePos;
    }
  return -1;
}

void cLiveFileWriter::Clear(void)
{
  Lock();
  buffer = NULL;
  filePos = 0;
  full = false;
  writeFile->Seek(0,0);
  Unlock();
}

// --- cLiveCutterThread -----------------------------------------------------

cLiveCutterThread::cLiveCutterThread(const char *FileName, int FileNumber, cLiveBuffer *LiveBuffer, int StartFrame, int EndFrame)
{
  readFile = NULL;
 
  writeFileName = new cFileName64(FileName, true);
  writeFileName->SetNumber(FileNumber);
  writeFile = writeFileName->Open();

  liveBuffer = LiveBuffer;
  startFrame = StartFrame;
  endFrame = EndFrame;
  Start();
}

cLiveCutterThread::~cLiveCutterThread()
{
  Cancel(3);
  if (readFile)
  readFileName->Close();
  writeFileName->Close();
}

void cLiveCutterThread::Action(void)
{
  int Frame = startFrame;
  int readPos = 0;
  int writePos = 0;
  while (Running() && Frame < endFrame) {
    uchar pt;
    liveBuffer->index->RwLock->Lock(false);
    int size = liveBuffer->index->Size(Frame, &pt);
    if (pt == I_FRAME && writePos > MEGABYTE(Setup.MaxVideoFileSize)) {
      int Number = writeFileName->Number();
      writeFileName->Close();
      writeFileName->SetNumber(Number+1);
      writeFile = writeFileName->Open();
      writePos = 0; 
      }
    if (liveBuffer->index->IsWritten(Frame)) {
      if (!readFile) {
        readFileName = new cFileName64(liveBuffer->filestring, false);
        readFileName->SetNumber(liveBuffer->fileWriter->FileNumber());
        readFile = readFileName->Open();
        }
      uchar b[MAXFRAMESIZE];
      if (liveBuffer->index->GetOffset(Frame) != readPos) {
        readPos = liveBuffer->index->GetOffset(Frame);
        readFile->Seek(readPos,0);
        }
      int c = readFile->Read(b, size);
      readPos += c;
      writeFile->Write(b,c);
      writePos+=size;
      if (c != size)
        writeFile->Seek(writePos,0);
      }
    else {
      writeFile->Write(&liveBuffer->buffer[liveBuffer->index->GetOffset(Frame)], size);
      writePos+=size;
      }
    liveBuffer->index->RwLock->Unlock();
    Frame++; 
    cCondWait::SleepMs(5);
    }
  if (readFile)
     readFileName->Close();
  writeFileName->Close();
}

// --- cLiveBuffer -----------------------------------------------------------

cLiveBuffer::cLiveBuffer(const char *FileName, cRemux *Remux, const cChannel *Channel, int Number)
:cThread("livebuffer")
{
  number = Number;
  bufSize = (Setup.InRAM && Number == 0) ? MEGABYTE(Setup.MemBufSize) : LIVEBUFSIZE;
  buffer = MALLOC(uchar, bufSize);  
  writeHD = !(Setup.InRAM && Number == 0);
  lastRead = 0;
  if (!buffer) {
     dsyslog("Couldn't reserve memory for livebuffer\n");
     bufSize = LIVEBUFSIZE;
     buffer = MALLOC(uchar, bufSize);
     }
  channel = Channel;
  index = new cLiveIndex(bufSize);
  remux = Remux;
  filestring = strdup(FileName);
  head = tail = blank = 0;
  startFrame = -1;
  liveCutter = NULL;
  fileWriter = NULL;
  fileReader = NULL;
  if (writeHD) {  
     fileWriter = new cLiveFileWriter(FileName);
     fileReader = new cLiveFileReader(FileName, fileWriter->FileNumber());
     }
  status = 0;
  Start();
}

cLiveBuffer::~cLiveBuffer()
{
  Cancel(3);
  delete liveCutter;
  delete fileReader;
  delete index;
  delete fileWriter;
  free(filestring);
  free(buffer);
}

void cLiveBuffer::SetStatus(int s)
{
  if (s == -1 || status != 2 && s == 0)
     status = 0;
  if (status == 0 && s == 1)
     status = 1;
  if (s == 2)
     status = 2;
  if (s == -2 && status == 0)
     status = -1;
}

void cLiveBuffer::Write(bool Wait)
{
  int wcount = index->NextWrite();
  off64_t pos=fileWriter->Write(&buffer[tail], wcount, Wait);
  if (pos>=0) {
     index->WrittenTo(pos-wcount, wcount);
     tail += wcount;
     if (tail + MAXFRAMESIZE > bufSize) {
       tail = 0;
       blank = 0;
       index->SetBlank(blank);
       }
     if (fileWriter->Full())
       index->Delete(pos);
     }
}

void cLiveBuffer::Store(uchar *Data, int Count)
{

  if (pictureType != NO_PICTURE) {
    if (head + MAXFRAMESIZE > bufSize) {
      blank = bufSize - head; 
      head = 0;
      index->SetBlank(blank);
      }
    index->Add(pictureType,head);
    }
  else
    index->AddData(head);
  memcpy(&buffer[head], Data, Count);
  head += Count;

  int free = head >= tail ? max(tail,bufSize-head) : tail - head;

  while (free < MAXFRAMESIZE) {
    if (writeHD)
       Write(true);
    else {
       tail = index->DelFirst();
       if (tail == 0) {
         blank = 0;
         index->SetBlank(blank);
         }
       if (Setup.ExtendBuffer && lastRead < index->delCount+25 && status >= 0) {
         writeHD = true;
         fileWriter = new cLiveFileWriter(filestring);
         fileReader = new cLiveFileReader(filestring, fileWriter->FileNumber());
         }
       }
    free = head >= tail ? max(tail,bufSize-head) : tail - head;
    }
}

void cLiveBuffer::Action(void)
{  
  while (Running()) {
    int Count;
    Lock();
    uchar *p = remux->Get(Count, &pictureType);
    if (p) {
      Store(p,Count);
      remux->Del(Count);
      }
    Unlock();
    if (writeHD) {
       int free = head >= tail ? max(tail,bufSize-head) : tail - head;
       if (free < 2*MAXFRAMESIZE)
          Write(false);
       }
    }  
      
}

void cLiveBuffer::SetNewRemux(cRemux *Remux, const cChannel *Channel)
{
  if (Channel != channel) {
    index->Switched();
    }
  channel = Channel;
  if (!Remux) {
    Cancel(3);
    return;
    }
  if (fileReader) fileReader->Clear();
  if (!Running() && Remux) {
    remux = Remux;
    Start();
    }
  else {
    Cancel(-1);
    Lock();
    if (writeHD && Setup.InRAM && number == 0 && !status) {
       index->Clear();
       delete fileReader; fileReader = NULL;
       delete fileWriter; fileWriter = NULL; 
       writeHD = false;
       }
    remux = Remux;
    Unlock();
    Start();
    }
}

int cLiveBuffer::GetFrame(uchar **Buffer, int Number, int Off)
{
  if (!Buffer) {
    if (fileReader) fileReader->Clear();
    return -1;
    }
  if (Number < FirstIndex())
     return -2;
  if (!index->HasFrame(Number) && (Off < 0 || !index->HasFrame(Number-1)))
     return -1;
  lastRead = Number;
  if (!index->IsWritten(Number)) {   
    index->RwLock->Lock(false);
    int size=index->Size(Number);
    int off = index->GetOffset(Number);
    if (off >= 0) {
      if (Off>=0) {
        size -= Off;
        off += Off;
        }
      if (size > 0) {
      uchar *b = MALLOC(uchar, size);
      *Buffer = b;
      memcpy(b,&buffer[off],size);
         }
      if (!index->HasFrame(Number)) 
         size = -size;
      }
    else
       size = -1;
    index->RwLock->Unlock();
    return size; 
    }
  else 
    return fileReader->Read(Buffer,index->GetOffset(Number),index->Size(Number));  
  return -1;  
}

void cLiveBuffer::CreateIndexFile(const char *FileName, int64_t PTS, int EndFrame)
{
  if (liveCutter) {
     if (liveCutter->Active()) {
        startFrame = -1;
        return;
        }
     else {
        delete liveCutter;
        liveCutter = NULL;
        }
     }
  if (startFrame < 0)
     return;
  cIndexFile *indexFile = new cIndexFile(FileName, true);
  if (!indexFile)
     return;
  int endFrame = EndFrame ? EndFrame : index->FindIFrame(PTS, buffer); 
  int timeout = 0;
  while (endFrame < 0 && timeout < 50) {    
    cCondWait::SleepMs(100);   
    endFrame = index->FindIFrame(PTS, buffer);
    timeout++;
    }

  char *data="LiveBuffer";
  cFileName fileName(FileName, true);
  cUnbufferedFile *file = fileName.Open();
  file->Write(data,10);
  int Number = fileName.Number();
  int FileOffset=0;
  for (int Frame = startFrame; Frame < endFrame; Frame++) {
    uchar pt;
    int size = index->Size(Frame, &pt);
    if (pt == I_FRAME && FileOffset > MEGABYTE(Setup.MaxVideoFileSize)) {
      file = fileName.NextFile();   
      file->Write(data,10);
      FileOffset=0;
      }
    indexFile->Write(pt,fileName.Number(),FileOffset);
    FileOffset+=size;
    }
  liveCutter = new cLiveCutterThread(FileName,Number,this,startFrame,endFrame);
  startFrame = -1;
  delete indexFile;
}

// --- cLiveReceiver ---------------------------------------------------------

cLiveReceiver::cLiveReceiver(const cChannel *Channel)
:cReceiver(Channel->GetChannelID(), -1, Channel->Vpid(), Channel->Apids(), Setup.UseDolbyDigital ? Channel->Dpids() : NULL, Channel->Spids())
{
  channel = Channel;
  vpid = Channel->Vpid();
  for (int i = 0; i < MAXAPIDS; i++)       
     apids[i]=Channel->Apid(i);
  for (int i = 0; i < MAXDPIDS; i++)
     dpids[i]= Setup.UseDolbyDigital ? Channel->Dpid(i) : 0;
  for (int i = 0; i < MAXSPIDS; i++)
     spids[i]=Channel->Spid(i);
  ringBuffer = new cRingBufferLinear(RECORDERBUFSIZE, TS_SIZE * 2, true, "Recorder");
  ringBuffer->SetTimeouts(0, 20);
  remux = new cRemux(Channel->Vpid(), Channel->Apids(), Setup.UseDolbyDigital ? Channel->Dpids() : NULL, Channel->Spids());
  remux->SetTimeouts(0, 50);
  attached = false;
}

cLiveReceiver::~cLiveReceiver()
{
  Detach();
  Cancel(3);
  delete remux;
  delete ringBuffer;
}

void cLiveReceiver::Activate(bool On)
{
  if (On) {
     Start();
     attached = true;
     }
  else {
     Cancel(-1);
     attached = false;
  } 
}

void cLiveReceiver::Receive(uchar *Data, int Length)
{
  int p = ringBuffer->Put(Data, Length);
  if (p != Length && Running())
     ringBuffer->ReportOverflow(Length - p);
}

void cLiveReceiver::Action(void)
{
  while (Running()) {
        int r;
        uchar *b = ringBuffer->Get(r);
        if (b) {
           int Count = remux->Put(b, r);
           if (Count)
              ringBuffer->Del(Count);
           else
              cCondWait::SleepMs(20);
           }
        }
}

bool cLiveReceiver::IsReceiving(const cChannel *Channel)
{
  if (vpid != Channel->Vpid())
     return false;
  for (int i = 0; i < MAXAPIDS; i++)
     if (apids[i] != Channel->Apid(i))
        return false;
  for (int i = 0; i < MAXDPIDS && Setup.UseDolbyDigital; i++)
     if (dpids[i] != Channel->Dpid(i))
        return false;
  for (int i = 0; i < MAXSPIDS; i++)
     if (spids[i] != Channel->Spid(i))
        return false;

  return true;
}

// --- cLiveBackTrace --------------------------------------------------------

#define AVG_FRAME_SIZE 15000 
#define DVB_BUF_SIZE   (256 * 1024) 
#define BACKTRACE_ENTRIES (DVB_BUF_SIZE / AVG_FRAME_SIZE + 20)

class cLiveBackTrace {
private:
  int index[BACKTRACE_ENTRIES];
  int length[BACKTRACE_ENTRIES];
  int pos, num;
public:
  cLiveBackTrace(void);
  void Clear(void);
  void Add(int Index, int Length);
  int Get(bool Forward);
  };

cLiveBackTrace::cLiveBackTrace(void)
{
  Clear();
}

void cLiveBackTrace::Clear(void)
{
  pos = num = 0;
}

void cLiveBackTrace::Add(int Index, int Length)
{
  index[pos] = Index;
  length[pos] = Length;
  if (++pos >= BACKTRACE_ENTRIES)
     pos = 0;
  if (num < BACKTRACE_ENTRIES)
     num++;
}

int cLiveBackTrace::Get(bool Forward)
{
  int p = pos;
  int n = num;
  int l = DVB_BUF_SIZE + (Forward ? 0 : 256 * 1024);
  int i = -1;

  while (n && l > 0) {
        if (--p < 0)
           p = BACKTRACE_ENTRIES - 1;
        i = index[p] - 1;
        l -= length[p];
        n--;
        }
  return i;
}

// --- cLivePlayer -----------------------------------------------------------

#define PLAYERBUFSIZE  MEGABYTE(1)

#define MAX_VIDEO_SLOWMOTION 63
#define NORMAL_SPEED  4
#define MAX_SPEEDS    3
#define SPEED_MULT   12
int cLivePlayer::Speeds[] = { 0, -2, -4, -8, 1, 2, 4, 12, 0 };

cLivePlayer::cLivePlayer(cLiveBuffer *LiveBuffer)
:cThread("liveplayer")
{
  liveBuffer = LiveBuffer;
  ringBuffer = new cRingBufferFrame(PLAYERBUFSIZE);
  readIndex = writeIndex = -1;
  readFrame = playFrame = NULL;
  firstPacket = true;
  playMode = pmPlay;
  playDir = pdForward;
  trickSpeed = NORMAL_SPEED;
  Off = 0;
  backTrace = new cLiveBackTrace;
  liveBuffer->SetStatus(0);
}

cLivePlayer::~cLivePlayer()
{
  liveBuffer->SetStatus(-2);
  Detach();
  Cancel(3);
  liveBuffer->GetFrame(NULL,0);
  delete readFrame;
  delete backTrace;
  delete ringBuffer;
}

void cLivePlayer::TrickSpeed(int Increment)
{
  int nts = trickSpeed + Increment;
  if (Speeds[nts] == 1) {
     trickSpeed = nts;
     if (playMode == pmFast)
        Play();
     else
        Pause();
     }
  else if (Speeds[nts]) {
     trickSpeed = nts;
     int Mult = (playMode == pmSlow && playDir == pdForward) ? 1 : SPEED_MULT;
     int sp = (Speeds[nts] > 0) ? Mult / Speeds[nts] : -Speeds[nts] * Mult;
     if (sp > MAX_VIDEO_SLOWMOTION)
        sp = MAX_VIDEO_SLOWMOTION;
     DeviceTrickSpeed(sp);
     }
}

void cLivePlayer::Empty(void)
{
  LOCK_THREAD;
  liveBuffer->GetFrame(NULL,0);
  if ((readIndex = backTrace->Get(playDir == pdForward)) < 0)
     readIndex = writeIndex;
  delete readFrame; 
  readFrame = NULL;
  playFrame = NULL;
  ringBuffer->Clear();
  DeviceClear();
  backTrace->Clear();
  firstPacket = true;
  Off = 0;
}

void cLivePlayer::Activate(bool On)
{
  if (On) 
     Start();
  else
     Cancel(-1);
}

void cLivePlayer::Action(void)
{
  int PollTimeouts = 0;
  uchar *b = NULL;
  uchar **pb = &b;
  uchar *p = NULL;
  int pc = 0;

  if (liveBuffer->Stay())
     readIndex = liveBuffer->LastRead();
  else
     readIndex = liveBuffer->LastIndex();

  bool Sleep = false;
  bool WaitingForData = false;  

  while (Running()) {
    if (Sleep) {
      if (WaitingForData)
         cCondWait::SleepMs(3);  
      else
         cCondWait::SleepMs(3);
      Sleep = false;
      }

   cPoller Poller;
   if (!DevicePoll(Poller, 100)) {
     PollTimeouts++;
     continue;
     }
   else
     PollTimeouts = 0;

   if (PollTimeouts >= 6) {
     DeviceClear();
     PlayPes(NULL, 0);
     }
 
   LOCK_THREAD;

   if (playMode != pmStill && playMode != pmPause) {
     if (!readFrame) {
       if (playMode == pmFast || (playMode == pmSlow && playDir == pdBackward)) { 
         int Index = liveBuffer->GetNextIFrame(readIndex, playDir == pdForward);
         if (Index < 0) {
           if (!DeviceFlush(100))
              continue;
           DevicePlay();
           playMode = pmPlay;
           playDir = pdForward;
           continue;
           }
         int r = liveBuffer->GetFrame(pb, Index);
         if (r>0) {
           readIndex = Index;
           WaitingForData = false;
           readFrame = new cFrame(b, -r, ftUnknown, readIndex);
           b = NULL;
           }
         else
           WaitingForData = true; 
         }
       else {
          int r=liveBuffer->GetFrame(pb, readIndex+1, Off);
          if (r>0) 
            readIndex++;
          if (r > 0 || r < -10) {
            WaitingForData = false;
            readFrame = new cFrame(b, -abs(r), ftUnknown, readIndex);
            b = NULL;
            if (r<0) 
               Off += -r;
            else
               Off = 0;
            }
          else if (r==-2)
             readIndex = liveBuffer->GetNextIFrame(liveBuffer->FirstIndex(), true)-1;
          else  
            WaitingForData = true;
          }
       }
      if (readFrame) {
        if (ringBuffer->Put(readFrame))
          readFrame = NULL;
        }
      }
    else
      Sleep = true;        
    if (!playFrame) {
      playFrame = ringBuffer->Get();
      p = NULL;
      pc = 0;
      }
    if (playFrame) {
      if (!p) {
         p = playFrame->Data();
         pc = playFrame->Count();
         if (p) {
            if (firstPacket) {
               PlayPes(NULL, 0);
               cRemux::SetBrokenLink(p, pc);
               firstPacket = false;
               }
             }
          }
       if (p) {
          int w = PlayPes(p, pc, playMode != pmPlay);
          if (w > 0) {
             p += w;
             pc -= w;
             }
          else {
             if (w < 0 && FATALERRNO) {
                LOG_ERROR;
                break;
                }
             }
          }
       if (pc == 0) {
          writeIndex = playFrame->Index();
          backTrace->Add(playFrame->Index(), playFrame->Count());
          ringBuffer->Drop(playFrame);
          playFrame = NULL;
          p = NULL;
          } 
      } 
    else
      Sleep = true;  
  }
}

void cLivePlayer::Pause(void)
{
  if (playMode == pmPause || playMode == pmStill)
     Play();
  else {
     liveBuffer->SetStatus(1);
     LOCK_THREAD;
     if (playMode == pmFast || (playMode == pmSlow && playDir == pdBackward))
        Empty();
     DeviceFreeze();
     playMode = pmPause;
     }
}

void cLivePlayer::Play(void)
{
  if (playMode != pmPlay) {
     liveBuffer->SetStatus(0);
     LOCK_THREAD;
     if (playMode == pmStill || playMode == pmFast || (playMode == pmSlow && playDir == pdBackward))
        Empty();
     DevicePlay();
     playMode = pmPlay;
     playDir = pdForward;
    }
}

void cLivePlayer::Forward(void)
{
  switch (playMode) {
    case pmFast:
         if (Setup.MultiSpeedMode) {
            TrickSpeed(playDir == pdForward ? 1 : -1);
            break;
            }
         else if (playDir == pdForward) {
            Play();
            break;
            }
    case pmPlay: {
         LOCK_THREAD;
         Empty();
         DeviceMute();
         playMode = pmFast;
         playDir = pdForward;
         trickSpeed = NORMAL_SPEED;
         TrickSpeed(Setup.MultiSpeedMode ? 1 : MAX_SPEEDS);
         }
         break;
    case pmSlow:
         if (Setup.MultiSpeedMode) {
            TrickSpeed(playDir == pdForward ? -1 : 1);
            break;
            }
         else if (playDir == pdForward) {
            Pause();
            break;
            }
    case pmStill:
    case pmPause:
         DeviceMute();
         playMode = pmSlow;
         playDir = pdForward;
         trickSpeed = NORMAL_SPEED;
         TrickSpeed(Setup.MultiSpeedMode ? -1 : -MAX_SPEEDS);
         break;
    }
}

void cLivePlayer::Backward(void)
{
  switch (playMode) {
    case pmFast:
         if (Setup.MultiSpeedMode) {
            TrickSpeed(playDir == pdBackward ? 1 : -1);
            break;
            }
         else if (playDir == pdBackward) {
            Play();
            break;
            }
    case pmPlay: {
         LOCK_THREAD;
         Empty();
         DeviceMute();
         playMode = pmFast;
         playDir = pdBackward;
         trickSpeed = NORMAL_SPEED;
         TrickSpeed(Setup.MultiSpeedMode ? 1 : MAX_SPEEDS);
         }
         break;
    case pmSlow:
         if (Setup.MultiSpeedMode) {
            TrickSpeed(playDir == pdBackward ? -1 : 1);
            break;
            }
         else if (playDir == pdBackward) {
            Pause();
            break;
            }
    case pmStill:
    case pmPause: {
         LOCK_THREAD;
         Empty();
         DeviceMute();
         playMode = pmSlow;
         playDir = pdBackward;
         trickSpeed = NORMAL_SPEED;
         TrickSpeed(Setup.MultiSpeedMode ? -1 : -MAX_SPEEDS);
         }
         break;
    }
}

bool cLivePlayer::Stop(void)
{
  liveBuffer->SetStatus(-1);
  if (readIndex >= liveBuffer->LastIndex() - 25)
     return false;
  LOCK_THREAD;
  Empty();
  readIndex = writeIndex = liveBuffer->GetNextIFrame(liveBuffer->LastIndex()-1, false)-1;
  Play();
  return true;
}

void cLivePlayer::SkipSeconds(int Seconds)
{
  if (Seconds) {
     LOCK_THREAD;
     Empty();
     int Index = writeIndex;
     if (Index >= 0) {
        Index = max(Index + Seconds * FRAMESPERSEC, liveBuffer->FirstIndex());
        if (Index > liveBuffer->FirstIndex())
           Index = liveBuffer->GetNextIFrame(Index, false);
        if (Index >= 0)
           readIndex = writeIndex = Index - 1; 
        }
     Play();
     }
}

bool cLivePlayer::GetIndex(int &Current, int &Total, bool SnapToIFrame)
{
  if (liveBuffer) {
    if (playMode == pmStill)
       Current = max(readIndex, 0);
    else {
      Current = max(writeIndex, 0); 
      // TODO SnapToIFrame
      }
    Total = liveBuffer->LastIndex() - liveBuffer->FirstIndex();
    Current -= liveBuffer->FirstIndex();
    return true;
    }
  Current = Total = -1;
  return false;
}

bool cLivePlayer::GetReplayMode(bool &Play, bool &Forward, int &Speed)
{
  Play = (playMode == pmPlay || playMode == pmFast);
  Forward = (playDir == pdForward);
  if (playMode == pmFast || playMode == pmSlow)
     Speed = Setup.MultiSpeedMode ? abs(trickSpeed - NORMAL_SPEED) : 0 ; 
  else
     Speed = -1;
  return true;
}

// --- cLiveBufferControl ----------------------------------------------------

cLiveBufferControl::cLiveBufferControl(cLivePlayer *Player)
:cControl(Player, true)
{
  player = Player;
  displayReplay = NULL;
  visible = modeOnly = shown = false;
  lastCurrent = lastTotal = -1;
  lastPlay = lastForward = false;
  lastSpeed = -2; 
  timeoutShow = 0;
}

cLiveBufferControl::~cLiveBufferControl()
{
  Hide();
  cTransferControl::receiverDevice = NULL;
  cLiveBufferManager::livePlayer = NULL;
  delete player;
  cLiveBufferManager::liveControl = NULL;
  if (!Setup.LiveBuffer) 
     cLiveBufferManager::Shutdown();
}

bool cLiveBufferControl::GetIndex(int &Current, int &Total, bool SnapToIFrame)
{
  if (player) {
     player->GetIndex(Current, Total, SnapToIFrame);
     return true;
     }
  return false;

}

bool cLiveBufferControl::GetReplayMode(bool &Play, bool &Forward, int &Speed)
{
  return player && player->GetReplayMode(Play, Forward, Speed);
}

void cLiveBufferControl::ShowTimed(int Seconds)
{
  if (modeOnly)
     Hide();
  if (!visible) {
     shown = ShowProgress(true);
     timeoutShow = (shown && Seconds > 0) ? time(NULL) + Seconds : 0;
     }
}

void cLiveBufferControl::Show(void)
{
  ShowTimed();
}

void cLiveBufferControl::Hide(void)
{
  if (visible) {
     delete displayReplay;
     displayReplay = NULL;
     SetNeedsFastResponse(false);
     visible = false;
     modeOnly = false;
     lastPlay = lastForward = false;
     lastSpeed = -2; // an invalid value
//     timeSearchActive = false;
     }
}

void cLiveBufferControl::ShowMode(void)
{
  if (visible || Setup.ShowReplayMode && !cOsd::IsOpen()) {
     bool Play, Forward;
     int Speed;
     if (GetReplayMode(Play, Forward, Speed) && (!visible || Play != lastPlay || Forward != lastForward || Speed != lastSpeed)) {
        bool NormalPlay = (Play && Speed == -1);

        if (!visible) {
           if (NormalPlay)
              return; 
           visible = modeOnly = true;
           displayReplay = Skins.Current()->DisplayReplay(modeOnly);
           }

        if (modeOnly && !timeoutShow && NormalPlay)
           timeoutShow = time(NULL) + 3;
        displayReplay->SetMode(Play, Forward, Speed);
        lastPlay = Play;
        lastForward = Forward;
        lastSpeed = Speed;
        }
     }
}

bool cLiveBufferControl::ShowProgress(bool Initial)
{
  int Current, Total;

  if (GetIndex(Current, Total) && Total > 0) {
     if (!visible) {
        displayReplay = Skins.Current()->DisplayReplay(modeOnly);
        SetNeedsFastResponse(true);
        visible = true;
        }
     if (Initial) {
        lastCurrent = lastTotal = -1;
        }
     if (Total != lastTotal) {
        displayReplay->SetTotal(IndexToHMSF(Total));
        if (!Initial)
           displayReplay->Flush();
        }
     if (Current != lastCurrent || Total != lastTotal) {
        displayReplay->SetProgress(Current, Total);
        if (!Initial)
           displayReplay->Flush();
int displayFrames = false;
        displayReplay->SetCurrent(IndexToHMSF(Current, displayFrames));
        displayReplay->Flush();
        lastCurrent = Current;
        }
     lastTotal = Total;
     ShowMode();
     return true;
     }
  return false;
}

eOSState cLiveBufferControl::ProcessKey(eKeys Key)
{
  if (visible) {
    if (timeoutShow && time(NULL) > timeoutShow) {
       Hide();
       ShowMode();
       timeoutShow = 0;
       }
    else if (modeOnly)
      ShowMode();
    else
      shown = ShowProgress(!shown) || shown;
    }
  bool DoShowMode = true;
  switch (Key) {
        case kDown:   if (!visible || modeOnly)
                         return osUnknown;
        case kPause:  if (player)
                        player->Pause();
                      break;
        case kPlay:  if (visible && !modeOnly) {
                        Hide();
                        DoShowMode = true;
                        }
                     else 
                        Show();
        case kUp:    if (Key == kUp && (!visible || modeOnly))
                        return osUnknown;
                     if (player)
                        player->Play(); 
                     break;
        case kLeft|k_Release: 
                       if (!visible || modeOnly)
                          return osUnknown;
        case kFastRew|k_Release:
                       if (Setup.MultiSpeedMode) break;
        case kLeft:    if (Key == kLeft && (!visible || modeOnly))
                          return osUnknown;
        case kFastRew: if (player)
                          player->Backward(); 
                       break;
        case kRight|k_Release:
                       if (!visible || modeOnly)
                          return osUnknown;
        case kFastFwd|k_Release:
                       if (Setup.MultiSpeedMode) break;
        case kRight:   if (Key == kRight && (!visible || modeOnly))
                          return osUnknown;
        case kFastFwd: if (player)
                          player->Forward();
                       break;
        case kGreen|k_Repeat:
        case kGreen:   if (visible && !modeOnly && player)
                          player->SkipSeconds(-60);
                       else
                          return osUnknown;
                       break;
        case kYellow|k_Repeat:
        case kYellow:  if (visible && !modeOnly && player)
                          player->SkipSeconds(60);
                       else
                          return osUnknown;
                       break;  
    default: {
      DoShowMode = false;
      switch (Key) {
        case kStop:    if (player) 
                          if (player->Stop())
                             break;
                       return osUnknown;
        case kBack:    if (visible && !modeOnly && player)
                          Hide();
                       else
                          return osUnknown;
                       break;
        case kRecord:  {
                       cTimer *timer = new cTimer(true);
                       int Current, Total;
                       GetIndex(Current,Total);
                       time_t start = time(NULL) - (Total - Current) / FRAMESPERSEC;
                       time_t t = start;
                       struct tm tm_r;
                       struct tm *now = localtime_r(&t, &tm_r);
                       timer->start = now->tm_hour * 100 + now->tm_min;
                       timer->stop = now->tm_hour * 60 + now->tm_min + Setup.InstantRecordTime;
                       timer->stop = (timer->stop / 60) * 100 + (timer->stop % 60);
                       if (timer->stop >= 2400)
                          timer->stop -= 2400;
                       timer->day = cTimer::SetTime(start, 0);
                       timer->channel = Channels.GetByNumber(cDevice::CurrentChannel());
                       Timers.Add(timer);
                       Timers.SetModified();
                       if (cRecordControls::Start(timer))
                          Skins.Message(mtInfo, tr("Recording started"));
                       break;
                       }
        default: return osUnknown;
        }
      }
    }
  if (DoShowMode)
     ShowMode();
  return osContinue;
}

// --- cLiveBufferManager ----------------------------------------------------

cLiveReceiver *cLiveBufferManager::liveReceiver[MAXLIVEBUFFERS] = { NULL };
cLiveBuffer *cLiveBufferManager::liveBuffer[MAXLIVEBUFFERS] = { NULL };
cDevice *cLiveBufferManager::receiverDevice[MAXLIVEBUFFERS] = { NULL };
cLivePlayer *cLiveBufferManager::livePlayer = NULL;
cLiveBufferControl *cLiveBufferManager::liveControl = NULL;

void cLiveBufferManager::ChannelSwitch(cDevice *ReceiverDevice, const cChannel *Channel)
{
  int i;
  for (i=0; i<MAXLIVEBUFFERS; i++)
     if (liveBuffer[i] && liveBuffer[i]->Channel() == Channel)
        break;  
  if (i == MAXLIVEBUFFERS) {
     if (Setup.InRAM && !liveBuffer[0])
        i = 0;
     else
        for (i=0; i<MAXLIVEBUFFERS; i++)
            if (liveBuffer[i] && (!liveBuffer[i]->Stay() || !liveReceiver[i]->IsAttached()))
               break;
     if (i == MAXLIVEBUFFERS) {
        for (i = 0; i<MAXLIVEBUFFERS; i++)
          if (!liveBuffer[i])
             break;
        }
     }
  if (liveBuffer[i]) {
     if (!(liveReceiver[i]->IsReceiving(Channel) && liveReceiver[i]->IsAttached())) {
        cLiveReceiver *oldReceiver = liveReceiver[i];
        liveReceiver[i] = new cLiveReceiver(Channel);
        ReceiverDevice->AttachReceiver(liveReceiver[i]);
        liveBuffer[i]->SetNewRemux(liveReceiver[i]->remux, Channel);
        delete oldReceiver;
        receiverDevice[i] = ReceiverDevice;
        }
     else if (liveBuffer[i]->Stay())
        liveBuffer[i]->SetStatus(2);
     }
  else {
     liveReceiver[i] = new cLiveReceiver(Channel);
     ReceiverDevice->AttachReceiver(liveReceiver[i]);
     receiverDevice[i] = ReceiverDevice;
     char FileName[256];
     sprintf(FileName,"%s/LiveBuffer",BufferDirectory);
     liveBuffer[i] = new cLiveBuffer(FileName,liveReceiver[i]->remux, Channel, i);
     }
  delete liveControl;  

  cTransferControl::receiverDevice = receiverDevice[i];

  livePlayer = new cLivePlayer(liveBuffer[i]);
  cControl::Launch(liveControl = new cLiveBufferControl(livePlayer));

  for (int j = 0; j < MAXLIVEBUFFERS; j++)
     if (i!=j && liveBuffer[j] && !liveBuffer[j]->LiveCutterActive() && (!liveBuffer[j]->Stay() || !liveReceiver[j]->IsAttached())) {
        delete liveBuffer[j]; 
        liveBuffer[j] = NULL;
        delete liveReceiver[j];
        liveReceiver[j] = NULL;
        }
}

void cLiveBufferManager::Shutdown(void)
{
  delete liveControl;
  for (int i = 0; i < MAXLIVEBUFFERS; i++) {
      delete liveBuffer[i];
      liveBuffer[i] = NULL;
      delete liveReceiver[i];
      liveReceiver[i] = NULL;
      }
  char FileName[256];
  sprintf(FileName,"%s/LiveBuffer",BufferDirectory);
  RemoveFileOrDir(FileName, true);
}

cLiveBuffer *cLiveBufferManager::InLiveBuffer(cTimer *timer, int *StartFrame, int *EndFrame)
{
  bool recstart = false;
  if (timer && timer->HasFlags(tfRecording)) {
    timer->SetFlags(tfhasLiveBuf);
    recstart = true;
    }
  for (int i = 0; i < MAXLIVEBUFFERS; i++)
  if (timer && timer->HasFlags(tfActive) && !timer->HasFlags(tfVps) && (!timer->HasFlags(tfhasLiveBuf) || recstart) && liveBuffer[i] && liveReceiver[i]->GetChannel() == timer->Channel()) {
    int now = liveBuffer[i]->LastIndex();
    int first = liveBuffer[i]->FirstIndex() + 50;
    if (now-first < 250)  // Erst wenn LiveBuffer gr��er 10s
       return NULL;
    if (timer->StartTime() < time(NULL) && now-first > (time(NULL)-timer->StopTime())*FRAMESPERSEC) {
       if (StartFrame) {
          if ((time(NULL)-timer->StartTime())*FRAMESPERSEC < now - first)
             *StartFrame = now - (time(NULL) - timer->StartTime())*FRAMESPERSEC;
          else
             *StartFrame = first;
          }
       if (EndFrame) {
         if (time(NULL) > timer->StopTime())
           *EndFrame = now - (time(NULL) - timer->StopTime()) * FRAMESPERSEC;
         else
           *EndFrame = 0;     
         }
       return liveBuffer[i]; 
       }
    }
  return NULL;
}

int cLiveBufferManager::Impact(cDevice *device, const cChannel *Channel, bool LiveView)
{
  if (!Setup.LiveBuffer)
     return 0;
  int r = 1;
  for (int i=0; i < MAXLIVEBUFFERS; i++)
     if (liveBuffer[i] && receiverDevice[i] == device) {
        if (liveBuffer[i]->Stay() && liveReceiver[i]->IsAttached())
           if (liveBuffer[i]->Channel()->Transponder() != Channel->Transponder() ||
               liveBuffer[i]->Channel()->Source() != Channel->Source())
                  r = 2;
        }
  return r;
}

bool cLiveBufferManager::AllowsChannelSwitch(void)
{
  return true; //  return !liveBuffer || !liveBuffer->LiveCutterActive();
}
#endif /* LIVEBUFFER */
