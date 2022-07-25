#include <sdw.h>
#include <zstd.h>

#include SDW_MSC_PUSH_PACKED
struct SArhHeader
{
	u32 Signature;
	u32 StringSize;
	u32 NodeCount;
	u32 StringSectionOffset;
	u32 StringSectionSize;
	u32 NodeSectionOffset;
	u32 NodeSectionSize;
	u32 FileInfoSectionOffset;
	u32 FileInfoCount;
	u32 Key0;
	u32 Reserved[2];
} SDW_GNUC_PACKED;

struct SNode
{
	n32 Value;
	n32 ParentIndex;
} SDW_GNUC_PACKED;

struct SFileInfo
{
	n64 Offset;
	u32 Size;
	u32 UncompressedSize;
	u32 CompressType;
	u32 Index;
} SDW_GNUC_PACKED;

struct SCompressedDataHeader
{
	u32 Signature;
	u32 Unknown0x4;
	u32 UncompressedSize;
	u32 CompressedSize;
	u32 Unknown0xC;
	char Name[0x1C];
} SDW_GNUC_PACKED;
#include SDW_MSC_POP_PACKED

struct SNode2
{
	SNode Node;
	bool Named;
	string Name;
	SNode2()
		: Named(false)
	{
	}
};

struct SFileInfo2
{
	SFileInfo FileInfo;
	string Path;
	u32 UncompressedSize;
};

int unpackAr(const UChar* a_pArhFileName, const UChar* a_pArdFileName, const UChar* a_pDirName)
{
	FILE* fp = UFopen(a_pArhFileName, USTR("rb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	u32 uArhSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	u8* pArh = new u8[uArhSize];
	fread(pArh, 1, uArhSize, fp);
	fclose(fp);
	SArhHeader* pArhHeader = reinterpret_cast<SArhHeader*>(pArh);
	if (pArhHeader->Signature != SDW_CONVERT_ENDIAN32('arh1'))
	{
		delete[] pArh;
		return 1;
	}
	if (static_cast<u32>(Align(pArhHeader->StringSize, 4)) != pArhHeader->StringSectionSize)
	{
		delete[] pArh;
		return 1;
	}
	if (pArhHeader->StringSectionOffset != static_cast<u32>(sizeof(SArhHeader)))
	{
		delete[] pArh;
		return 1;
	}
	if (pArhHeader->NodeSectionOffset != pArhHeader->StringSectionOffset + pArhHeader->StringSectionSize)
	{
		delete[] pArh;
		return 1;
	}
	if (pArhHeader->NodeSectionSize != pArhHeader->NodeCount * static_cast<u32>(sizeof(SNode)))
	{
		delete[] pArh;
		return 1;
	}
	if (pArhHeader->FileInfoSectionOffset != static_cast<u32>(Align(pArhHeader->NodeSectionOffset + pArhHeader->NodeSectionSize, 16)))
	{
		delete[] pArh;
		return 1;
	}
	if (static_cast<u32>(Align(pArhHeader->FileInfoSectionOffset + pArhHeader->FileInfoCount * sizeof(SFileInfo), 16)) != uArhSize)
	{
		delete[] pArh;
		return 1;
	}
	for (u32 i = 0; i < SDW_ARRAY_COUNT(pArhHeader->Reserved); i++)
	{
		if (pArhHeader->Reserved[i] != 0)
		{
			delete[] pArh;
			return 1;
		}
	}
	char* pStringSection = reinterpret_cast<char*>(pArh + pArhHeader->StringSectionOffset);
	u8* pNodeSection = reinterpret_cast<u8*>(pArh + pArhHeader->NodeSectionOffset);
	SNode* pNode = reinterpret_cast<SNode*>(pNodeSection);
	SFileInfo* pFileInfo = reinterpret_cast<SFileInfo*>(pArh + pArhHeader->FileInfoSectionOffset);
	u32 uKey32 = pArhHeader->StringSize ^ *reinterpret_cast<u32*>(pStringSection);
	if ((uKey32 ^ pArhHeader->Key0) != 0xF3F35353)
	{
		delete[] pArh;
		return 1;
	}
	static const u32 c_uKeySize = 4;
	vector<u8> vKey(c_uKeySize);
	vKey[0] = uKey32 & 0xFF;
	vKey[1] = uKey32 >> 8 & 0xFF;
	vKey[2] = uKey32 >> 16 & 0xFF;
	vKey[3] = uKey32 >> 24 & 0xFF;
	for (u32 i = 0; i < pArhHeader->StringSectionSize; i++)
	{
		pStringSection[i] ^= vKey[i % c_uKeySize];
	}
	for (u32 i = 0; i < pArhHeader->NodeSectionSize; i++)
	{
		pNodeSection[i] ^= vKey[i % c_uKeySize];
	}
	vector<SNode2> vNode2(pArhHeader->NodeCount);
	vector<SFileInfo2> vFileInfo2(pArhHeader->FileInfoCount);
	for (u32 i = 0; i < pArhHeader->NodeCount; i++)
	{
		stack<u32> sIndex;
		sIndex.push(i);
		while (!sIndex.empty())
		{
			u32 uNodeIndex = sIndex.top();
			SNode& node = pNode[uNodeIndex];
			SNode2& node2 = vNode2[uNodeIndex];
			if (node2.Named)
			{
				sIndex.pop();
				continue;
			}
			node2.Node = node;
			string sName;
			if (node.ParentIndex >= 0)
			{
				SNode2& parentNode2 = vNode2[node.ParentIndex];
				if (!parentNode2.Named)
				{
					sIndex.push(node.ParentIndex);
					continue;
				}
				sName = parentNode2.Name;
				u32 uChar = parentNode2.Node.Value ^ uNodeIndex;
				if (uChar >= 0x100)
				{
					delete[] pArh;
					return 1;
				}
				sName.push_back(static_cast<char>(uChar));
				if (node.Value < 0)
				{
					n32 nStringOffset = -node.Value;
					string sNameSuffix = pStringSection + nStringOffset;
					nStringOffset += static_cast<n32>(sNameSuffix.size()) + 1;
					u32 uFileInfoIndex = *reinterpret_cast<u32*>(pStringSection + nStringOffset);
					sName += sNameSuffix;
					SFileInfo2& fileInfo2 = vFileInfo2[uFileInfoIndex];
					fileInfo2.FileInfo = pFileInfo[uFileInfoIndex];
					fileInfo2.Path = sName;
				}
			}
			node2.Named = true;
			node2.Name = sName;
			sIndex.pop();
		}
	}
	for (u32 i = 0; i < pArhHeader->FileInfoCount; i++)
	{
		SFileInfo2& fileInfo2 = vFileInfo2[i];
		SFileInfo& fileInfo = fileInfo2.FileInfo;
		if (fileInfo.Index != i)
		{
			delete[] pArh;
			return 1;
		}
		if (fileInfo.CompressType != 0 && fileInfo.CompressType != 2)
		{
			delete[] pArh;
			return 1;
		}
		switch (fileInfo.CompressType)
		{
		case 0:
			if (fileInfo.UncompressedSize != 0)
			{
				delete[] pArh;
				return 1;
			}
			fileInfo2.UncompressedSize = fileInfo.Size;
			break;
		case 2:
			if (fileInfo.UncompressedSize == 0)
			{
				delete[] pArh;
				return 1;
			}
			fileInfo2.UncompressedSize = fileInfo.UncompressedSize;
			break;
		}
	}
	// test begin
	FILE* fpTest = UFopen(USTR("decrypted.arh"), USTR("wb"), false);
	if (fpTest != nullptr)
	{
		fwrite(pArh, 1, uArhSize, fpTest);
		fclose(fpTest);
	}
	fpTest = UFopen(USTR("node.tsv"), USTR("wb"), false);
	if (fpTest != nullptr)
	{
		for (u32 i = 0; i < pArhHeader->NodeCount; i++)
		{
			SNode2& node2 = vNode2[i];
			fprintf(fpTest, "%d\t%d\t%d\t%s\r\n", i, node2.Node.Value, node2.Node.ParentIndex, node2.Name.c_str());
		}
		fclose(fpTest);
	}
	// test end
	fp = UFopen(a_pArdFileName, USTR("rb"), false);
	if (fp == nullptr)
	{
		delete[] pArh;
		return 1;
	}
	for (u32 i = 0; i < pArhHeader->FileInfoCount; i++)
	{
		SFileInfo2& fileInfo2 = vFileInfo2[i];
		SFileInfo& fileInfo = fileInfo2.FileInfo;
		Fseek(fp, fileInfo.Offset, SEEK_SET);
		SCompressedDataHeader compressedDataHeader;
		u8* pCompressedData = new u8[fileInfo.Size];
		u8* pUncompressedData = nullptr;
		switch (fileInfo.CompressType)
		{
		case 0:
			{
				fread(pCompressedData, 1, fileInfo.Size, fp);
				pUncompressedData = pCompressedData;
			}
			break;
		case 2:
			{
				fread(&compressedDataHeader, sizeof(compressedDataHeader), 1, fp);
				if (compressedDataHeader.Signature != SDW_CONVERT_ENDIAN32('xbc1'))
				{
					delete[] pCompressedData;
					fclose(fp);
					delete[] pArh;
					return 1;
				}
				if (compressedDataHeader.Unknown0x4 != 3)
				{
					delete[] pCompressedData;
					fclose(fp);
					delete[] pArh;
					return 1;
				}
				if (compressedDataHeader.UncompressedSize != fileInfo.UncompressedSize)
				{
					delete[] pCompressedData;
					fclose(fp);
					delete[] pArh;
					return 1;
				}
				if (compressedDataHeader.CompressedSize != fileInfo.Size)
				{
					delete[] pCompressedData;
					fclose(fp);
					delete[] pArh;
					return 1;
				}
				u32 uNameSize = static_cast<u32>(strlen(compressedDataHeader.Name));
				if (uNameSize >= SDW_ARRAY_COUNT(compressedDataHeader.Name))
				{
					delete[] pCompressedData;
					fclose(fp);
					delete[] pArh;
					return 1;
				}
				for (u32 j = uNameSize; j < SDW_ARRAY_COUNT(compressedDataHeader.Name); j++)
				{
					if (compressedDataHeader.Name[j] != 0)
					{
						delete[] pCompressedData;
						fclose(fp);
						delete[] pArh;
						return 1;
					}
				}
				fread(pCompressedData, 1, fileInfo.Size, fp);
				pUncompressedData = new u8[compressedDataHeader.UncompressedSize];
				size_t uUncompressedSize = ZSTD_decompress(pUncompressedData, compressedDataHeader.UncompressedSize, pCompressedData, compressedDataHeader.CompressedSize);
				if (static_cast<u32>(uUncompressedSize) != compressedDataHeader.UncompressedSize)
				{
					delete[] pUncompressedData;
					delete[] pCompressedData;
					fclose(fp);
					delete[] pArh;
					return 1;
				}
				delete[] pCompressedData;
			}
			break;
		}
		UString sFileName = a_pDirName;
		sFileName += U8ToU(fileInfo2.Path);
		UString sDirName = sFileName;
		UString::size_type uPos = sDirName.find_last_of(USTR("/\\"));
		if (uPos != UString::npos)
		{
			sDirName.erase(uPos);
		}
		if (!UMakeDir(sDirName))
		{
			delete[] pUncompressedData;
			fclose(fp);
			delete[] pArh;
			return 1;
		}
		FILE* fpSub = UFopen(sFileName.c_str(), USTR("wb"), false);
		if (fpSub == nullptr)
		{
			delete[] pUncompressedData;
			fclose(fp);
			delete[] pArh;
			return 1;
		}
		fwrite(pUncompressedData, 1, fileInfo2.UncompressedSize, fpSub);
		fclose(fpSub);
		delete[] pUncompressedData;
	}
	fclose(fp);
	delete[] pArh;
	return 0;
}

int UMain(int argc, UChar* argv[])
{
	if (argc != 5)
	{
		return 1;
	}
	if (UCslen(argv[1]) == 1)
	{
		switch (*argv[1])
		{
		case USTR('U'):
		case USTR('u'):
			return unpackAr(argv[2], argv[3], argv[4]);
		default:
			break;
		}
	}
	return 1;
}
