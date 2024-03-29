/*
 * File Name:		transfer_routines.c
 * Description:		The routines that transfer data between each ringbuffer
 * Date:			2022.2.9
 * Author:			HBSnail
 */

#include "global.h"
#include "transfer_routines.h"
#include "ring_buffer.h"
#include "filter_subroutines.h"

extern RING_BUFFER kernel2userRingBuffer_INBOUND;
extern RING_BUFFER kernel2userRingBuffer_OUTBOUND;
extern RING_BUFFER user2kernelRingBuffer_INBOUND;
extern RING_BUFFER user2kernelRingBuffer_OUTBOUND;


extern LOOKASIDE_LIST_EX ringBufferBlockPoolList;
extern NDIS_HANDLE filterDriverHandle;

BOOLEAN threadFLAG;

VOID TransmitRoutine_INBOUND(VOID* must_null_ptr) {
	BYTE* dataBuffer = ExAllocateFromLookasideListEx(&ringBufferBlockPoolList);
	MDL* dataMDL = NdisAllocateMdl(filterDriverHandle, dataBuffer, RING_BUFFER_BLOCK_SIZE);

	while (threadFLAG) {

		if (dataBuffer == NULL) {
			return;
		}
		do {

			NTSTATUS status = ReadBlockFromRingBuffer(&user2kernelRingBuffer_INBOUND, dataBuffer);


			if (!NT_SUCCESS(status)) {
				break;
			}

			//Ring buffer block structure:
			//ifIndex 4 byte;  ethLeng 4Byte; ethdata... ;pending 0000....

			ULONG interfaceIndex = *(ULONG*)(dataBuffer+ 0);
			ULONG ethLength = *(ULONG*)(dataBuffer + 4);

			FILTER_CONTEXT* fContext = GetFilterContextByMiniportInterfaceIndex(interfaceIndex);
			if (fContext == NULL) {
				break;
			}

			NdisMoveMemory(dataBuffer, dataBuffer + 8, ethLength);
			TransmitEthPacket(fContext, ethLength, dataMDL, FilterToUpper, NO_FLAG);

		} while (FALSE);

	}


	if (dataBuffer != NULL) {
		ExFreeToLookasideListEx(&ringBufferBlockPoolList, dataBuffer);
	}
	if (dataMDL != NULL) {
		NdisFreeMdl(dataMDL);
	}


	DbgPrint("THREAD TERMINATE\n");
	NTSTATUS s = PsTerminateSystemThread(STATUS_SUCCESS);
	DbgPrint("THREAD TERMINATE %d\n", s);

}

VOID TransmitRoutine_OUTBOUND(VOID* must_null_ptr) {
	BYTE* dataBuffer = ExAllocateFromLookasideListEx(&ringBufferBlockPoolList);
	MDL* dataMDL = NdisAllocateMdl(filterDriverHandle, dataBuffer, RING_BUFFER_BLOCK_SIZE);
	while (threadFLAG) {

		if (dataBuffer == NULL) {
			return;
		}
		do {

			NTSTATUS status = ReadBlockFromRingBuffer(&user2kernelRingBuffer_OUTBOUND, dataBuffer);


			if (!NT_SUCCESS(status)) {
				break;
			}

			//Ring buffer block structure:
			//ifIndex 4 byte;  ethLeng 4Byte; ethdata... ;pending 0000....

			ULONG interfaceIndex = *(ULONG*)(dataBuffer + 0);
			ULONG ethLength = *(ULONG*)(dataBuffer + 4);

			FILTER_CONTEXT* fContext = GetFilterContextByMiniportInterfaceIndex(interfaceIndex);
			if (fContext == NULL) {
				break;
			}

			NdisMoveMemory(dataBuffer, dataBuffer + 8, ethLength);

			TransmitEthPacket(fContext, ethLength, dataMDL, FilterToNIC, NO_FLAG);

		} while (FALSE);

	}


	if (dataBuffer != NULL) {
		ExFreeToLookasideListEx(&ringBufferBlockPoolList, dataBuffer);
	}

	if (dataMDL != NULL) {
		NdisFreeMdl(dataMDL);
	}

	DbgPrint("THREAD TERMINATE\n");
	NTSTATUS s = PsTerminateSystemThread(STATUS_SUCCESS);
	DbgPrint("THREAD TERMINATE %d\n", s);

}


NTSTATUS InitTransferRoutine(){

	NTSTATUS status = STATUS_SUCCESS;

	do {
		//Init the ring buffer which can share data with Ring3
		//20 means 1<<20 Bytes = 1MB
		//Init ring buffer with size of 1MB

		status = InitRingBuffer(&kernel2userRingBuffer_INBOUND, 20, &(UNICODE_STRING)RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\winptables_ke_k2u_in"));

		if (!NT_SUCCESS(status)) {
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			break;
		}

		status = InitRingBuffer(&kernel2userRingBuffer_OUTBOUND, 20, &(UNICODE_STRING)RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\winptables_ke_k2u_out"));

		if (!NT_SUCCESS(status)) {
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			break;
		}

		status = InitRingBuffer(&user2kernelRingBuffer_INBOUND, 20, &(UNICODE_STRING)RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\winptables_ke_u2k_in"));

		if (!NT_SUCCESS(status)) {
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_INBOUND);
			break;
		}

		status = InitRingBuffer(&user2kernelRingBuffer_OUTBOUND, 20, &(UNICODE_STRING)RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\winptables_ke_u2k_out"));

		if (!NT_SUCCESS(status)) {
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_INBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_OUTBOUND);
			break;
		}

		//Create thread handling the user2kernelRingBuffer
		threadFLAG = TRUE;
		HANDLE readingThread = NULL;
		status = PsCreateSystemThread(&readingThread, 0, NULL, NULL, NULL, (PKSTART_ROUTINE)TransmitRoutine_INBOUND, NULL);
		if (!NT_SUCCESS(status)) {
			threadFLAG = FALSE;
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_INBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_OUTBOUND);
			break;
		}
		status = ZwClose(readingThread);
		if (!NT_SUCCESS(status)) {
			threadFLAG = FALSE;
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_INBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_OUTBOUND);
			break;
		}

		status = PsCreateSystemThread(&readingThread, 0, NULL, NULL, NULL, (PKSTART_ROUTINE)TransmitRoutine_OUTBOUND, NULL);
		if (!NT_SUCCESS(status)) {
			threadFLAG = FALSE;
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_INBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_OUTBOUND);
			break;
		}
		status = ZwClose(readingThread);
		if (!NT_SUCCESS(status)) {
			threadFLAG = FALSE;
			FreeRingBuffer(&kernel2userRingBuffer_INBOUND);
			FreeRingBuffer(&kernel2userRingBuffer_OUTBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_INBOUND);
			FreeRingBuffer(&user2kernelRingBuffer_OUTBOUND);
			break;
		}
		

	} while (FALSE);

	return status;

}