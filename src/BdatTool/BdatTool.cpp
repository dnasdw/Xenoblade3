#include <sdw.h>

#include SDW_MSC_PUSH_PACKED
struct SBdat0Header
{
	u32 Signature;
	u32 Unknown0x4;
	u32 Count;
	u32 Size;
	u32 Bdat1Offset[1];
} SDW_GNUC_PACKED;

struct SBdat1Header
{
	u32 Signature;
	u32 Unknown0x4;
	u32 ColumnCount;
	u32 RowCount;
	u32 BeginRowId;
	u32 Unknown0x14;
	u32 ColumnDescriptionOffset;
	u32 MurmurHash3IndexOffset;
	u32 DataOffset;
	u32 RowByteCount;
	u32 ExtraOffset;
	u32 ExtraSize;
} SDW_GNUC_PACKED;

enum EColumnType
{
	kCloumnTypeU8 = 1,
	kCloumnTypeU16 = 2,
	kCloumnTypeU32 = 3,
	kCloumnTypeN8 = 4,
	kCloumnTypeN16 = 5,
	kCloumnTypeN32 = 6,
	kCloumnTypeString = 7,
	kCloumnTypeF32 = 8,
	kCloumnTypeHash = 9,
	kCloumnTypeUnknown32bit = 11,
	kCloumnTypeUnknown16bit = 13,
	kColumnTypeCount
};

struct SColumnDescription
{
	u8 ColumnType;
	u16 NameMurmurHash3Offset;
} SDW_GNUC_PACKED;

struct SMurmurHash3Index
{
	u32 MurmurHash3;
	u32 Index;
} SDW_GNUC_PACKED;
#include SDW_MSC_POP_PACKED

wstring addQuotation(wstring a_sValue, bool a_bForce = true)
{
	if (a_bForce || a_sValue.find_first_of(L",\"\r\n") != wstring::npos)
	{
		a_sValue = L"\"" + Replace(a_sValue, L'\"', L"\"\"") + L"\"";
	}
	return a_sValue;
}

int exportBdat(const UChar* a_pBdatFileName, const UChar* a_pCsvFileNamePrefix)
{
	FILE* fp = UFopen(a_pBdatFileName, USTR("rb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	u32 uBdat0Size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	u8* pBdat0 = new u8[uBdat0Size];
	fread(pBdat0, 1, uBdat0Size, fp);
	fclose(fp);
	SBdat0Header* pBdat0Header = reinterpret_cast<SBdat0Header*>(pBdat0);
	if (pBdat0Header->Signature != SDW_CONVERT_ENDIAN32('BDAT'))
	{
		delete[] pBdat0;
		return 1;
	}
	if (pBdat0Header->Unknown0x4 != 0x01001004)
	{
		delete[] pBdat0;
		return 1;
	}
	if (pBdat0Header->Count == 0)
	{
		delete[] pBdat0;
		return 1;
	}
	if (pBdat0Header->Size != uBdat0Size)
	{
		delete[] pBdat0;
		return 1;
	}
	if (pBdat0Header->Bdat1Offset[0] != sizeof(SBdat0Header) + (pBdat0Header->Count - 1) * 4)
	{
		delete[] pBdat0;
		return 1;
	}
	for (u32 i = 0; i < pBdat0Header->Count; i++)
	{
		u32 uBdat1Offset = pBdat0Header->Bdat1Offset[i];
		u8* pBdat1 = pBdat0 + uBdat1Offset;
		SBdat1Header* pBdat1Header = reinterpret_cast<SBdat1Header*>(pBdat1);
		if (pBdat1Header->Signature != SDW_CONVERT_ENDIAN32('BDAT'))
		{
			delete[] pBdat0;
			return 1;
		}
		if (pBdat1Header->Unknown0x4 != 0x3004)
		{
			delete[] pBdat0;
			return 1;
		}
		if (pBdat1Header->ColumnCount == 0)
		{
			delete[] pBdat0;
			return 1;
		}
		if (pBdat1Header->ColumnDescriptionOffset < static_cast<u32>(sizeof(SBdat1Header)))
		{
			delete[] pBdat0;
			return 1;
		}
		if (pBdat1Header->MurmurHash3IndexOffset != pBdat1Header->ColumnDescriptionOffset + pBdat1Header->ColumnCount * static_cast<u32>(sizeof(SColumnDescription)))
		{
			delete[] pBdat0;
			return 1;
		}
		if (pBdat1Header->DataOffset != pBdat1Header->MurmurHash3IndexOffset + pBdat1Header->RowCount * static_cast<u32>(sizeof(SMurmurHash3Index)))
		{
			delete[] pBdat0;
			return 1;
		}
		if (pBdat1Header->ExtraOffset != pBdat1Header->DataOffset + pBdat1Header->RowCount * pBdat1Header->RowByteCount)
		{
			delete[] pBdat0;
			return 1;
		}
		u32 uStringSizeMin = 9 + pBdat1Header->ColumnCount * 4;
		if (pBdat1Header->ExtraSize < uStringSizeMin)
		{
			delete[] pBdat0;
			return 1;
		}
		u8* pData = pBdat1 + pBdat1Header->DataOffset;
		char* pExtraData = reinterpret_cast<char*>(pBdat1)+pBdat1Header->ExtraOffset;
		u32 uCheckRowByteCount = 0;
		SColumnDescription* pColumnDescription = reinterpret_cast<SColumnDescription*>(pBdat1 + pBdat1Header->ColumnDescriptionOffset);
		for (u32 j = 0; j < pBdat1Header->ColumnCount; j++)
		{
			SColumnDescription& columnDescription = pColumnDescription[j];
			switch (columnDescription.ColumnType)
			{
			case kCloumnTypeU8:
			case kCloumnTypeN8:
				uCheckRowByteCount++;
				break;
			case kCloumnTypeU16:
			case kCloumnTypeN16:
			case kCloumnTypeUnknown16bit:
				uCheckRowByteCount += 2;
				break;
			case kCloumnTypeU32:
			case kCloumnTypeN32:
			case kCloumnTypeString:
			case kCloumnTypeF32:
			case kCloumnTypeHash:
			case kCloumnTypeUnknown32bit:
				uCheckRowByteCount += 4;
				break;
			default:
				delete[] pBdat0;
				return 1;
			}
			if (columnDescription.NameMurmurHash3Offset != 9 + j * 4)
			{
				delete[] pBdat0;
				return 1;
			}
		}
		if (pBdat1Header->RowByteCount != uCheckRowByteCount)
		{
			delete[] pBdat0;
			return 1;
		}
		UString sCsvFileName = a_pCsvFileNamePrefix;
		if (pBdat0Header->Count != 1)
		{
			sCsvFileName += Format(USTR("_%d"), i);
		}
		if (pBdat1Header->Unknown0x14 != 0)
		{
			sCsvFileName += Format(USTR("_%08X"), pBdat1Header->Unknown0x14);
		}
		sCsvFileName += USTR(".csv");
		fp = UFopen(sCsvFileName.c_str(), USTR("wb"), false);
		if (fp == nullptr)
		{
			delete[] pBdat0;
			return 1;
		}
		fwrite("\xEF\xBB\xBF", 3, 1, fp);
		fprintf(fp, "__ID0__,__ID__");
		for (u32 j = 0; j < pBdat1Header->ColumnCount; j++)
		{
			SColumnDescription& columnDescription = pColumnDescription[j];
			u32 uNameMurmurHash3 = *reinterpret_cast<u32*>(pExtraData + columnDescription.NameMurmurHash3Offset);
			switch (columnDescription.ColumnType)
			{
			case kCloumnTypeU8:
			case kCloumnTypeU16:
			case kCloumnTypeU32:
			case kCloumnTypeN8:
			case kCloumnTypeN16:
			case kCloumnTypeN32:
				fprintf(fp, ",X_0x%08X,C_0x%08X", uNameMurmurHash3, uNameMurmurHash3);
				break;
			case kCloumnTypeString:
			case kCloumnTypeF32:
			case kCloumnTypeHash:
				fprintf(fp, ",C_0x%08X", uNameMurmurHash3);
				break;
			case kCloumnTypeUnknown32bit:
			case kCloumnTypeUnknown16bit:
				fprintf(fp, ",X_0x%08X,UC_0x%08X,NC_0x%08X", uNameMurmurHash3, uNameMurmurHash3, uNameMurmurHash3);
				break;
			}
		}
		fprintf(fp, "\r\n");
		fprintf(fp, "index,index");
		for (u32 j = 0; j < pBdat1Header->ColumnCount; j++)
		{
			fprintf(fp, ",");
			SColumnDescription& columnDescription = pColumnDescription[j];
			switch (columnDescription.ColumnType)
			{
			case kCloumnTypeU8:
				fprintf(fp, "hex8,u8");
				break;
			case kCloumnTypeU16:
				fprintf(fp, "hex16,u16");
				break;
			case kCloumnTypeU32:
				fprintf(fp, "hex32,u32");
				break;
			case kCloumnTypeN8:
				fprintf(fp, "hex8,n8");
				break;
			case kCloumnTypeN16:
				fprintf(fp, "hex16,n16");
				break;
			case kCloumnTypeN32:
				fprintf(fp, "hex32,n32");
				break;
			case kCloumnTypeString:
				fprintf(fp, "string");
				break;
			case kCloumnTypeF32:
				fprintf(fp, "f32");
				break;
			case kCloumnTypeHash:
				fprintf(fp, "hash");
				break;
			case kCloumnTypeUnknown32bit:
				fprintf(fp, "uhex32,uu32,un32");
				break;
			case kCloumnTypeUnknown16bit:
				fprintf(fp, "uhex16,uu16,un16");
				break;
			}
		}
		fprintf(fp, "\r\n");
		for (u32 j = 0; j < pBdat1Header->RowCount; j++)
		{
			fprintf(fp, "%d,%d", j, pBdat1Header->BeginRowId + j);
			u8* pRowData = pData + j * pBdat1Header->RowByteCount;
			n32 nCurrentByteIndex = 0;
			for (u32 k = 0; k < pBdat1Header->ColumnCount; k++)
			{
				fprintf(fp, ",");
				SColumnDescription& columnDescription = pColumnDescription[k];
				switch (columnDescription.ColumnType)
				{
				case kCloumnTypeU8:
					{
						u32 uValue = pRowData[nCurrentByteIndex];
						fprintf(fp, "\"%02X\",\"%u\"", uValue, uValue);
						nCurrentByteIndex++;
					}
					break;
				case kCloumnTypeU16:
					{
						u32 uValue = *reinterpret_cast<u16*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%04X\",\"%u\"", uValue, uValue);
						nCurrentByteIndex += 2;
					}
					break;
				case kCloumnTypeU32:
					{
						u32 uValue = *reinterpret_cast<u32*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%08X\",\"%u\"", uValue, uValue);
						nCurrentByteIndex += 4;
					}
					break;
				case kCloumnTypeN8:
					{
						n32 nValue = *reinterpret_cast<n8*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%02X\",\"%d\"", nValue & 0xFF, nValue);
						nCurrentByteIndex++;
					}
					break;
				case kCloumnTypeN16:
					{
						n32 nValue = *reinterpret_cast<n16*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%04X\",\"%d\"", nValue & 0xFFFF, nValue);
						nCurrentByteIndex += 2;
					}
					break;
				case kCloumnTypeN32:
					{
						n32 nValue = *reinterpret_cast<n32*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%08X\",\"%d\"", nValue, nValue);
						nCurrentByteIndex += 4;
					}
					break;
				case kCloumnTypeString:
					{
						u32 uStringOffset = *reinterpret_cast<u32*>(pRowData + nCurrentByteIndex);
						char* pString = reinterpret_cast<char*>(pExtraData + uStringOffset);
						fprintf(fp, "%s", WToU8(addQuotation(Replace(U8ToW(pString), L"\n", L"\r\n"))).c_str());
						nCurrentByteIndex += 4;
					}
					break;
				case kCloumnTypeF32:
					{
						f32 fValue = *reinterpret_cast<f32*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%f\"", fValue);
						nCurrentByteIndex += 4;
					}
					break;
				case kCloumnTypeHash:
					{
						u32 uValue = *reinterpret_cast<u32*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"H_0x%08X\"", uValue);
						nCurrentByteIndex += 4;
					}
					break;
				case kCloumnTypeUnknown32bit:
					{
						u32 uValue = *reinterpret_cast<u32*>(pRowData + nCurrentByteIndex);
						n32 nValue = *reinterpret_cast<n32*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%08X\",\"%u\",\"%d\"", uValue, uValue, nValue);
						nCurrentByteIndex += 4;
					}
					break;
				case kCloumnTypeUnknown16bit:
					{
						u32 uValue = *reinterpret_cast<u16*>(pRowData + nCurrentByteIndex);
						n32 nValue = *reinterpret_cast<n16*>(pRowData + nCurrentByteIndex);
						fprintf(fp, "\"%04X\",\"%u\",\"%d\"", uValue, uValue, nValue);
						nCurrentByteIndex += 2;
					}
					break;
				}
			}
			fprintf(fp, "\r\n");
		}
		fclose(fp);
	}
	delete[] pBdat0;
	return 0;
}

int UMain(int argc, UChar* argv[])
{
	if (argc != 4)
	{
		return 1;
	}
	if (UCslen(argv[1]) == 1)
	{
		switch (*argv[1])
		{
		case USTR('E'):
		case USTR('e'):
			return exportBdat(argv[2], argv[3]);
		default:
			break;
		}
	}
	return 1;
}
