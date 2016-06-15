#include "hooking.h"
#include "hook_file.h"
#include "monitor.h"
#include "utils.h"
#include "main.h"
#include "comm.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Description :
//  	Hide VBOX files
//  Parameters :
//  	See http://msdn.microsoft.com/en-us/library/cc512135%28v=vs.85%29.aspx
//  Return value :
//  	See http://msdn.microsoft.com/en-us/library/cc512135%28v=vs.85%29.aspx
//	Process :
//		if a malware tries to identify VirtualBox by trying to get attributes of vbox files, we return
//		INVALID_FILE_ATTRIBUTES.
//		we only log when there is an attempt to detect VirtualBox
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtQueryAttributesFile(__in POBJECT_ATTRIBUTES ObjectAttributes,
									  __out PFILE_BASIC_INFORMATION FileInformation)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId;
	UNICODE_STRING kObjectName;
	PWCHAR parameter = NULL; 
	
	PAGED_CODE();
	
	currentProcessId = (ULONG)PsGetCurrentProcessId();
	statusCall = Orig_NtQueryAttributesFile(ObjectAttributes, FileInformation);
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtQueryAttributesFile\n");

		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));
		
		if(NT_SUCCESS(statusCall))
		{
			__try
			{
				ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
				ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);
				ProbeForRead(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length, 1);
				
				kObjectName.Length = ObjectAttributes->ObjectName->Length;
				kObjectName.MaximumLength = ObjectAttributes->ObjectName->Length;
				kObjectName.Buffer = PoolAlloc(kObjectName.MaximumLength);

				RtlCopyUnicodeString(&kObjectName, ObjectAttributes->ObjectName);
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				exceptionCode = GetExceptionCode();
				if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,sss,FileHandle->0,buffer->ERROR,offset->0", exceptionCode)))
					sendLogs(currentProcessId, SIG_ntdll_NtQueryAttributesFile, parameter);
				else
					sendLogs(currentProcessId, SIG_ntdll_NtQueryAttributesFile, L"0,-1,sss,FileHandle->0,buffer->ERROR,offset->0");
				if(parameter != NULL)
					PoolFree(parameter);
				return statusCall;
			}			
		
			if(wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\drivers\\VBoxMouse.sys") || 
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\drivers\\VBoxGuest.sys") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\drivers\\VBoxSF.sys") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\drivers\\VBoxVideo.sys") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxControl.exe") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxDisp.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxHook.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxMRXNP.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGL.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGLarrayspu.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGLcrutil.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGLerrorspu.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGLfeedbackspu.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGLpackspu.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxOGLpassthroughspu.dll") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxService.exe") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\VBoxTray.exe") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\drivers\\vmmouse.sys") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Windows\\system32\\drivers\\vmhgfs.sys") ||
				wcsistr(kObjectName.Buffer, L"\\??\\C:\\Program Files\\oracle\\virtualbox guest additions\\"))
			{
				if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,-1,s,filepath->%wZ", &kObjectName)))
					sendLogs(currentProcessId, SIG_ntdll_NtQueryAttributesFile, parameter);
				else
					sendLogs(currentProcessId, SIG_ntdll_NtQueryAttributesFile, L"0,-1,s,filepath->ERROR");
				if(parameter != NULL)
					PoolFree(parameter);
				return INVALID_FILE_ATTRIBUTES;
			}
		}
		if(parameter != NULL)
			PoolFree(parameter);
	}
	return statusCall;	
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//  	Logs IOCTLs
//  Parameters :
//  	See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566441%28v=vs.85%29.aspx
//  Return value :
//  	See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566441%28v=vs.85%29.aspx
//	Process :
//		logs file handle, IoControlCode and both input and output buffer
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtDeviceIoControlFile(__in HANDLE FileHandle,
									  __in_opt HANDLE Event,
									  __in_opt PIO_APC_ROUTINE ApcRoutine,
									  __in_opt PVOID ApcContext,
									  __out PIO_STATUS_BLOCK IoStatusBlock,
									  __in ULONG IoControlCode,
									  __in_opt PVOID InputBuffer,
									  __in ULONG InputBufferLength,
									  __out_opt PVOID OutputBuffer,
									  __in ULONG OutputBufferLength)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId;
	PUCHAR kInputBuffer = NULL;
	PUCHAR kOutputBuffer = NULL;
	PWCHAR iBuff = NULL, oBuff = NULL;
	PWCHAR parameter = NULL; 
	USHORT log_lvl = LOG_ERROR;
	
	PAGED_CODE();

	currentProcessId = (ULONG)PsGetCurrentProcessId();
	statusCall = Orig_NtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtDeviceIoControlFile\n");

		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));

		__try
		{
			if(InputBuffer != NULL)
			{
				ProbeForRead(InputBuffer, InputBufferLength, 1);
				kInputBuffer = InputBuffer;
			}
			if(OutputBuffer != NULL)
			{
				ProbeForRead(OutputBuffer, OutputBufferLength, 1);
				kOutputBuffer = OutputBuffer;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			exceptionCode = GetExceptionCode();
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,ssss,InputBuffer->ERROR,FileHandle->0,ControlCode->0,OutputBuffer->ERROR", exceptionCode)))
				sendLogs(currentProcessId, SIG_ntdll_NtDeviceIoControlFile, parameter);
			else
				sendLogs(currentProcessId, SIG_ntdll_NtDeviceIoControlFile, L"0,-1,ssss,InputBuffer->ERROR,FileHandle->0,ControlCode->0,OutputBuffer->ERROR");
			if(parameter != NULL)
				PoolFree(parameter);
			return statusCall;
		}

		// log input and output buffer
		iBuff = PoolAlloc(BUFFER_LOG_MAX);
		//oBuff = PoolAlloc(BUFFER_LOG_MAX);
		CopyBuffer(iBuff, kInputBuffer, InputBufferLength);
		//CopyBuffer(oBuff, kOutputBuffer, OutputBufferLength);

		if(NT_SUCCESS(statusCall))
		{
			log_lvl = LOG_SUCCESS;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,ssss,InputBuffer->%ws,FileHandle->0x%08x,ControlCode->0x%08x,OutputBuffer->%ws", iBuff, FileHandle, IoControlCode, L"lol")))
				log_lvl = LOG_PARAM;
		}
		else
		{
			log_lvl = LOG_ERROR;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE,  L"0,%d,ssss,InputBuffer->%ws,FileHandle->0x%08x,ControlCode->0x%08x,OutputBuffer->%ws",statusCall, iBuff, FileHandle, IoControlCode, L"lol")))
				log_lvl = LOG_PARAM;
		}
			
		switch(log_lvl)
		{
			case LOG_PARAM:
				sendLogs(currentProcessId, SIG_ntdll_NtDeviceIoControlFile, parameter);
				break;
			case LOG_SUCCESS:
				sendLogs(currentProcessId, SIG_ntdll_NtDeviceIoControlFile, L"1,0,ssss,InputBuffer->ERROR,FileHandle->0,ControlCode->0,OutputBuffer->ERROR");
				break;
			default:
				sendLogs(currentProcessId, SIG_ntdll_NtDeviceIoControlFile, L"0,-1,ssss,InputBuffer->ERROR,FileHandle->0,ControlCode->0,OutputBuffer->ERROR");
		}

		if(iBuff != NULL)
			PoolFree(iBuff);
	//	if(oBuff != NULL)
		//	PoolFree(oBuff);
		if(parameter != NULL)
			PoolFree(parameter);
	}
	return statusCall;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Description :
//  	Dumps files which are about to be deleted (FILE_DELETE_ON_CLOSE)
//  Parameters :
//  	See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566417%28v=vs.85%29.aspx
//  Return value :
//  	See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566417%28v=vs.85%29.aspx
// 	Process :
//		if Handle is on the handle monitored list, retrieve filename from handle and move the file 
// 		to cuckoo directory in order to dump it
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtClose(__in HANDLE Handle)
{
	NTSTATUS statusCall;
	ULONG currentProcessId;
	UNICODE_STRING file_to_dump;
	PWCHAR parameter = NULL; 
	POBJECT_NAME_INFORMATION nameInformation = NULL;
	USHORT log_lvl = LOG_ERROR;
	
	PAGED_CODE();

	currentProcessId = (ULONG)PsGetCurrentProcessId();
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && IsHandleInList(Handle) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtClose\n");

		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));

		// retrieve filename from handle
		nameInformation = PoolAlloc(MAX_SIZE);
		if(nameInformation != NULL)
			ZwQueryObject(Handle, ObjectNameInformation, nameInformation, MAX_SIZE, NULL);
		
		// we need to move the file straight away (:
		if(nameInformation->Name.Buffer)
		{
			ZwClose(Handle);
			
			file_to_dump.Length = 0;
			file_to_dump.MaximumLength = NTSTRSAFE_UNICODE_STRING_MAX_CCH * sizeof(WCHAR);
			file_to_dump.Buffer = PoolAlloc(file_to_dump.MaximumLength);
			if(!NT_SUCCESS(dump_file(nameInformation->Name, &file_to_dump)))
				RtlInitUnicodeString(&file_to_dump, L"ERROR");
			
			if(parameter && nameInformation && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,ss,FileHandle->0x%08x,FileToDump->%wZ", Handle, &file_to_dump)))
				sendLogs(currentProcessId, SIG_ntdll_NtClose, parameter);
			else
				sendLogs(currentProcessId, SIG_ntdll_NtClose, L"0,-1,ss,FileHandle->0,FileToDump->ERROR");
		}
		
		if(nameInformation != NULL)
			PoolFree(nameInformation);
		
		if(parameter != NULL)
			PoolFree(parameter);
		
		RemoveHandleFromList(Handle);
		return Orig_NtClose(Handle);
	}
	else
		return Orig_NtClose(Handle);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//		Logs file deletion / rename.
//	Parameters :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff567096(v=vs.85).aspx
//	Return value :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff567096(v=vs.85).aspx
//	Process :
//		Copy the FileHandle parameter, then checks the FileInformationClass argument.
//		If FileDispositionInformation, the file may be deleted, the FileInformation->DeleteFile
//		parameter is copied and tested.
//		If FileRenameInformationrmation, the FileInformation->FileName parameter is copied along with the
//		FileInformation->RootDirectory parameter, then the call is logged.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
 NTSTATUS Hooked_NtSetInformationFile(__in HANDLE FileHandle,
									  __out PIO_STATUS_BLOCK IoStatusBlock,
									  __in PVOID FileInformation,
									  __in ULONG Length,
									  __in FILE_INFORMATION_CLASS FileInformationClass)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId;
	USHORT log_lvl = LOG_ERROR;
	UNICODE_STRING file_to_dump;
	UNICODE_STRING full_path;
	POBJECT_NAME_INFORMATION nameInformation = NULL;
	PWCHAR parameter = NULL;
	
	BOOLEAN kDeleteFile;
	IO_STATUS_BLOCK iosb;
	ULONG kFileNameLength;
	PFILE_RENAME_INFORMATION kFileRenameInformation = NULL;
	PWCHAR kFileName = NULL;
	HANDLE kRootDirectory = NULL;
	
	file_to_dump.Buffer = NULL;
	full_path.Buffer = NULL;
	
	PAGED_CODE();
	
	currentProcessId = (ULONG)PsGetCurrentProcessId();
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtSetInformationFile\n");
			
		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));
		
		// CHANGE FILE DISPOSITION INFORMATION CASE
		if(FileInformationClass == FileDispositionInformation)
		{
			__try
			{
				ProbeForRead(FileInformation, sizeof(FILE_DISPOSITION_INFORMATION), 1);
				kDeleteFile = ((PFILE_DISPOSITION_INFORMATION)FileInformation)->DeleteFile;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				exceptionCode = GetExceptionCode();
				if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,ss,FilePath->ERROR,FileToDump->ERROR", exceptionCode)))
					sendLogs(currentProcessId, SIG_kernel32_DeleteFileW, parameter);
				else 
					sendLogs(currentProcessId, SIG_kernel32_DeleteFileW, L"0,-1,ss,FilePath->ERROR,FileToDump->ERROR");
				if(parameter != NULL)
					PoolFree(parameter);
				return Orig_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
			}
			
			if(kDeleteFile == TRUE)
			{
				nameInformation = PoolAlloc(MAX_SIZE);
				if(nameInformation && parameter)
					ZwQueryObject(FileHandle, ObjectNameInformation, nameInformation, MAX_SIZE, NULL);
				
				// we need to move the file straight away
				if(nameInformation->Name.Buffer)
				{
					ZwClose(FileHandle);
					file_to_dump.Length = 0;
					file_to_dump.MaximumLength = NTSTRSAFE_UNICODE_STRING_MAX_CCH * sizeof(WCHAR);
					file_to_dump.Buffer = PoolAlloc(file_to_dump.MaximumLength);
					if(!NT_SUCCESS(dump_file(nameInformation->Name, &file_to_dump)))
						RtlInitUnicodeString(&file_to_dump, L"ERROR");
				}		
				log_lvl = LOG_SUCCESS;
				if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,ss,FilePath->%wZ,FileToDump->%wZ", &(nameInformation->Name), &file_to_dump)))
					log_lvl = LOG_PARAM;
				
				switch(log_lvl)
				{
					case LOG_PARAM:
						sendLogs(currentProcessId, SIG_kernel32_DeleteFileW, parameter);
					break;
					case LOG_SUCCESS:
						sendLogs(currentProcessId, SIG_kernel32_DeleteFileW, L"1,0,ss,FilePath->ERROR,FileToDump->ERROR");
					break;
					default:
						sendLogs(currentProcessId, SIG_kernel32_DeleteFileW, L"0,0,ss,FilePath->ERROR,FileToDump->ERROR");
					break;
				}
				if(nameInformation != NULL)
					PoolFree(nameInformation);
				if(parameter != NULL)
					PoolFree(parameter);
				
				return STATUS_SUCCESS;
			}
			else
				return Orig_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
		}
		
		statusCall = Orig_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
		
		// RENAME FILE CASE
		if(FileInformationClass == FileRenameInformation)
		{
			__try
			{
				ProbeForRead(FileInformation, sizeof(FILE_RENAME_INFORMATION), 1);
				ProbeForRead(((PFILE_RENAME_INFORMATION)FileInformation)->FileName, ((PFILE_RENAME_INFORMATION)FileInformation)->FileNameLength, 1);
				
				kFileRenameInformation = (PFILE_RENAME_INFORMATION)FileInformation;
				kRootDirectory = kFileRenameInformation->RootDirectory;
				kFileNameLength = kFileRenameInformation->FileNameLength;
				kFileName = PoolAlloc(kFileNameLength + sizeof(WCHAR));
				if(!kFileName)
				{
					sendLogs(currentProcessId, SIG_ntdll_NtSetInformationFile, L"0,-1,ssss,FileHandle->0,OriginalName->ERROR,RenamedName->ERROR,FileInformationClass->0");
					if(parameter)
						PoolFree(parameter);
					return statusCall;
				}
				RtlZeroMemory(kFileName, kFileNameLength + sizeof(WCHAR));
				RtlCopyMemory(kFileName, kFileRenameInformation->FileName, kFileNameLength);
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				exceptionCode = GetExceptionCode();
				if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"FileHandle->0,OriginalName->ERROR,RenamedName->ERROR,FileInformationClass->0")))
					sendLogs(currentProcessId, SIG_ntdll_NtSetInformationFile, parameter);
				else
					sendLogs(currentProcessId, SIG_ntdll_NtSetInformationFile, L"0,-1,ssss,FileHandle->0,OriginalName->ERROR,RenamedName->ERROR,FileInformationClass->0");
				if(parameter != NULL)
					PoolFree(parameter);
				if(kFileName != NULL)
					PoolFree(kFileName);
				return statusCall;
			}
			
			// handle the not null RootDirectory case
			if(kRootDirectory)
			{
				// allocate both name information struct and unicode string buffer
				nameInformation = PoolAlloc(MAX_SIZE);
				if(nameInformation)
				{
					if(NT_SUCCESS(ZwQueryObject(kRootDirectory, ObjectNameInformation, nameInformation, MAX_SIZE, NULL)) && kFileNameLength < 0xFFF0)
					{
						full_path.MaximumLength = nameInformation->Name.Length + (USHORT)kFileNameLength+2+sizeof(WCHAR);
						full_path.Buffer = PoolAlloc(full_path.MaximumLength);
						RtlZeroMemory(full_path.Buffer, full_path.MaximumLength);
						RtlCopyUnicodeString(&full_path, &(nameInformation->Name));
						RtlAppendUnicodeToString(&full_path, L"\\");
						RtlAppendUnicodeToString(&full_path, kFileName);
					}
				}
				else
					RtlInitUnicodeString(&full_path, kFileName);
			}
			else
				RtlInitUnicodeString(&full_path, kFileName);
			
			nameInformation = PoolAlloc(MAX_SIZE);
			if(nameInformation && parameter)
				ZwQueryObject(FileHandle, ObjectNameInformation, nameInformation, MAX_SIZE, NULL);
			
			if(NT_SUCCESS(statusCall))
			{
				log_lvl = LOG_SUCCESS;
				if(parameter && nameInformation && kFileName)
				{
					if(NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,ssss,FileHandle->0x%08x,OriginalName->%wZ,RenamedName->%wZ,FileInformationClass->%d", FileHandle, &(nameInformation->Name), &full_path, FileInformationClass)))
						log_lvl = LOG_PARAM;
				}
			}
			else
			{
				log_lvl = LOG_ERROR;
				if(parameter && nameInformation && kFileName)
				{
					if(NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,ssss,FileHandle->0x%08x,OriginalName->%wZ,RenamedName->%wZ,FileInformationClass->%d", statusCall, FileHandle, &(nameInformation->Name), &full_path, FileInformationClass)))
						log_lvl = LOG_PARAM;	
				}
			}		
			if(kFileName)
				PoolFree(kFileName);
			if(nameInformation)
				PoolFree(nameInformation);
			
			switch(log_lvl)
			{
				case LOG_PARAM:
					sendLogs(currentProcessId, SIG_ntdll_NtSetInformationFile, parameter);
				break;
				case LOG_SUCCESS:
					sendLogs(currentProcessId, SIG_ntdll_NtSetInformationFile, L"1,0,ssss,FileHandle->0,OriginalName->ERROR,RenamedName->ERROR,FileInformationClass->0");
				break;
				default:
					sendLogs(currentProcessId, SIG_ntdll_NtSetInformationFile, L"0,-1,ssss,FileHandle->0,OriginalName->ERROR,RenamedName->ERROR,FileInformationClass->0");
				break;
			}
		}
		if(parameter != NULL)
			PoolFree(parameter);
		return statusCall;	
	}
	else
		return Orig_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//		Logs file opening.
//	Parameters :
//		See http://msdn.microsoft.com/en-us/library/bb432381(v=vs.85).aspx
//  Return value :
//		See http://msdn.microsoft.com/en-us/library/bb432381(v=vs.85).aspx
//	Process :
//		Copies arguments, handles the non-NULL ObjectAttributes->RootDirectory parameter case (concat
//		of RootDirectory and ObjectName) then logs.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtOpenFile(__out PHANDLE FileHandle,
						   __in ACCESS_MASK DesiredAccess,
						   __in POBJECT_ATTRIBUTES ObjectAttributes,
						   __out PIO_STATUS_BLOCK IoStatusBlock,
						   __in ULONG ShareAccess,
						   __in ULONG OpenOptions)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId, returnLength;
	ULONG kShareAccess;
	USHORT log_lvl = LOG_ERROR;
	UNICODE_STRING full_path;
	PWCHAR parameter = NULL;
	POBJECT_NAME_INFORMATION nameInformation = NULL;
	HANDLE kRootDirectory, kFileHandle;
	UNICODE_STRING kObjectName;
	
	full_path.Buffer = NULL;
	kObjectName.Buffer = NULL;
	
	PAGED_CODE();
	
	currentProcessId = (ULONG)PsGetCurrentProcessId();
	
	kShareAccess = ShareAccess;
	ShareAccess |= FILE_SHARE_READ;
	
	statusCall = Orig_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtOpenFile\n");
			
		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));
		
		__try
		{

			ProbeForRead(FileHandle, sizeof(HANDLE), 1);
			ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
			ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);
			ProbeForRead(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length, 1);
		
			kFileHandle = *FileHandle;
			kRootDirectory = ObjectAttributes->RootDirectory;
			kObjectName.Length = ObjectAttributes->ObjectName->Length;
			kObjectName.MaximumLength = ObjectAttributes->ObjectName->MaximumLength;
			kObjectName.Buffer = PoolAlloc(kObjectName.MaximumLength);
			RtlCopyUnicodeString(&kObjectName, ObjectAttributes->ObjectName);	
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			exceptionCode = GetExceptionCode();
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,sssss,FileHandle->0,DesiredAccess->0,OpenOptions->0,ShareAccess->0,FilePath->ERROR", exceptionCode)))
				sendLogs(currentProcessId, SIG_ntdll_NtOpenFile, parameter);
			else 
				sendLogs(currentProcessId, SIG_ntdll_NtOpenFile, L"0,-1,sssss,FileHandle->0,DesiredAccess->0,OpenOptions->0,ShareAccess->0,FilePath->ERROR");
			if(parameter != NULL)
				PoolFree(parameter);
			return statusCall;
		}
	
		if(kRootDirectory)	// handle the not null rootdirectory case
		{
			// allocate both name information struct and unicode string buffer
			nameInformation = PoolAlloc(MAX_SIZE);
			if(nameInformation)
			{
				if(NT_SUCCESS(ZwQueryObject(kRootDirectory, ObjectNameInformation, nameInformation, MAX_SIZE, NULL)))
				{
					full_path.MaximumLength = nameInformation->Name.Length + kObjectName.Length + 2 + sizeof(WCHAR);
					full_path.Buffer = PoolAlloc(full_path.MaximumLength);
					RtlZeroMemory(full_path.Buffer, full_path.MaximumLength);
					RtlCopyUnicodeString(&full_path, &(nameInformation->Name));
					RtlAppendUnicodeToString(&full_path, L"\\");
					RtlAppendUnicodeStringToString(&full_path, &kObjectName);
				}
			}
		}
		else
			RtlInitUnicodeString(&full_path, kObjectName.Buffer);
		
		if(NT_SUCCESS(statusCall))
		{			
			log_lvl = LOG_SUCCESS;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,sssss,FileHandle->0x%08x,DesiredAccess->0x%08x,OpenOptions->0x%x,ShareAccess->0x%x,FilePath->%wZ", kFileHandle,DesiredAccess, OpenOptions, kShareAccess, &full_path)))
				log_lvl = LOG_PARAM;
		}
		else
		{
			log_lvl = LOG_ERROR;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE,  L"0,%d,sssss,FileHandle->0x%08x,DesiredAccess->0x%08x,OpenOptions->0x%x,ShareAccess->0x%x,FilePath->%wZ", statusCall, kFileHandle, DesiredAccess, OpenOptions, kShareAccess, &full_path)))
				log_lvl = LOG_PARAM;
		}
		
		switch(log_lvl)
		{
			case LOG_PARAM:
				sendLogs(currentProcessId, SIG_ntdll_NtOpenFile, parameter);
			break;
			case LOG_SUCCESS:
				sendLogs(currentProcessId, SIG_ntdll_NtOpenFile, L"1,0,sssss,FileHandle->0,DesiredAccess->0,OpenOptions->0,ShareAccess->0,FilePath->ERROR");
			break;
			default:
				sendLogs(currentProcessId, SIG_ntdll_NtOpenFile, L"0,-1,sssss,FileHandle->0,DesiredAccess->0,OpenOptions->0,ShareAccess->0,FilePath->ERROR");
			break;
		}
		if(parameter != NULL)
			PoolFree(parameter);
		if(nameInformation != NULL)
			PoolFree(nameInformation);
	}
	return statusCall;	
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//		Logs file deletion.
//	Parameters :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566435(v=vs.85).aspx
//	Return value :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566435(v=vs.85).aspx
//	Process :
//		Copies the ObjectAttributes->ObjectName parameter, copies the file about to be deleted in another
//		directory in order to dump it later and then logs the file deletion.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtDeleteFile(__in POBJECT_ATTRIBUTES ObjectAttributes)
{
	NTSTATUS exceptionCode;
	ULONG currentProcessId;
	USHORT log_lvl = LOG_ERROR;
	PWCHAR parameter = NULL;
	
	UNICODE_STRING kObjectName;
	UNICODE_STRING file_to_dump;
	
	PAGED_CODE();
	
	currentProcessId = (ULONG)PsGetCurrentProcessId();
		
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtDeleteFile()\n");
		
		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));
		
		__try
		{
			ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
			ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);
			ProbeForRead(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length, 1);
			
			kObjectName.Length = ObjectAttributes->ObjectName->Length;
			kObjectName.MaximumLength = ObjectAttributes->ObjectName->Length;
			kObjectName.Buffer = PoolAlloc(kObjectName.MaximumLength);
			
			if(kObjectName.Buffer)
				RtlCopyUnicodeString(&kObjectName, ObjectAttributes->ObjectName);
			else
			{
				sendLogs(currentProcessId, SIG_ntdll_NtDeleteFile, L"0,-1,ss,FileName->ERROR,FileToDump->ERROR");
				if(parameter != NULL)
					PoolFree(parameter);
				return Orig_NtDeleteFile(ObjectAttributes);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			exceptionCode = GetExceptionCode();
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,ss,FileName->ERROR,FileToDump->ERROR", exceptionCode)))
				sendLogs(currentProcessId, SIG_ntdll_NtDeleteFile, parameter);
			else
				sendLogs(currentProcessId, SIG_ntdll_NtDeleteFile, L"0,-1,ss,FileName->ERROR,FileToDump->ERROR");
			if(parameter != NULL)
				PoolFree(parameter);
			return Orig_NtDeleteFile(ObjectAttributes);
		}
		
		// dump file
		// we need to move the file straight away (:
		if(kObjectName.Buffer)
		{
			file_to_dump.Length = 0;
			file_to_dump.MaximumLength = NTSTRSAFE_UNICODE_STRING_MAX_CCH * sizeof(WCHAR);
			file_to_dump.Buffer = PoolAlloc(file_to_dump.MaximumLength);
			if(!NT_SUCCESS(dump_file(kObjectName, &file_to_dump)))
				RtlInitUnicodeString(&file_to_dump, L"ERROR");
		}
		
		log_lvl = LOG_SUCCESS;
		if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,ss,FileName->%wZ,FileToDump->%wZ", &kObjectName, &file_to_dump)))
			log_lvl = LOG_PARAM;
			
		switch(log_lvl)
		{
			case LOG_PARAM:
				sendLogs(currentProcessId, SIG_ntdll_NtDeleteFile, parameter);
				break;
			case LOG_SUCCESS:
				sendLogs(currentProcessId, SIG_ntdll_NtDeleteFile, L"1,0,ss,FileName->ERROR,FileToDump->ERROR");
				break;
			default:
				sendLogs(currentProcessId, SIG_ntdll_NtDeleteFile, L"1,0,ss,FileName->ERROR,FileToDump->ERROR");
		}
		if(parameter != NULL)
			PoolFree(parameter);
		return STATUS_SUCCESS;
	}
	return Orig_NtDeleteFile(ObjectAttributes);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//		Logs file reading.
//	Parameters :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff567072(v=vs.85).aspx
//	Return value :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff567072(v=vs.85).aspx
//	Process :
//		logs FileHandle, Length, ByteOffset and Buffer
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtReadFile(__in HANDLE FileHandle,
						   __in_opt HANDLE Event,
						   __in_opt PIO_APC_ROUTINE ApcRoutine,
						   __in_opt PVOID ApcContext,
						   __out PIO_STATUS_BLOCK IoStatusBlock,
						   __out PVOID Buffer,
						   __in ULONG Length,
						   __in_opt PLARGE_INTEGER ByteOffset,
						   __in_opt PULONG Key)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId, returnLength;
	USHORT log_lvl = LOG_ERROR;
	LARGE_INTEGER kByteOffset;
	ULONG_PTR kBufferSize;
	PUCHAR kBuffer = NULL;
	PWCHAR buff = NULL;
	PWCHAR parameter = NULL;
	kByteOffset.QuadPart = 0;

	PAGED_CODE();
	
	currentProcessId = (ULONG)PsGetCurrentProcessId();
	
	statusCall = Orig_NtReadFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtReadFile()\n");
		
		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));
		
		__try
		{
			ProbeForRead(IoStatusBlock, sizeof(IO_STATUS_BLOCK), 1);
			ProbeForRead((PVOID)IoStatusBlock->Information, sizeof(ULONG), 1);
			if(ByteOffset)
				kByteOffset = ProbeForReadLargeInteger(ByteOffset);
			kBufferSize = IoStatusBlock->Information;
			ProbeForRead(Buffer, kBufferSize, 1);
			kBuffer = Buffer;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			exceptionCode = GetExceptionCode();
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,sss,FileHandle->0,length->0,buffer->ERROR,offset->0", exceptionCode)))
				sendLogs(currentProcessId, SIG_ntdll_NtReadFile, parameter);
			else
				sendLogs(currentProcessId, SIG_ntdll_NtReadFile, L"0,-1,sss,FileHandle->0,length->0,buffer->ERROR,offset->0");
			if(parameter != NULL)
				PoolFree(parameter);
			return statusCall;
		}
		// log buffer
		buff = PoolAlloc(BUFFER_LOG_MAX);
		CopyBuffer(buff, kBuffer, kBufferSize);
		
		if(NT_SUCCESS(statusCall))
		{
			log_lvl = LOG_SUCCESS;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,ssss,FileHandle->0x%08x,length->%d,buffer->%ws,offset->%d", FileHandle, Length, buff, kByteOffset.QuadPart)))
				log_lvl = LOG_PARAM;
		}
		else
		{
			log_lvl = LOG_ERROR;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE,  L"0,%d,ssss,FileHandle->0x%08x,length->%d,buffer->%ws,offset->%d", statusCall, FileHandle, Length, buff, kByteOffset.QuadPart)))
				log_lvl = LOG_PARAM;
		}
			
		switch(log_lvl)
		{
			case LOG_PARAM:
				sendLogs(currentProcessId, SIG_ntdll_NtReadFile, parameter);
				break;
			case LOG_SUCCESS:
				sendLogs(currentProcessId, SIG_ntdll_NtReadFile, L"1,0,ssss,FileHandle->0,length->0,buffer->ERROR,offset->0");
				break;
			default:
				sendLogs(currentProcessId, SIG_ntdll_NtReadFile, L"0,-1,ssss,FileHandle->0,length->0,buffer->ERROR,offset->0");
		}

		if(parameter != NULL)
			PoolFree(parameter);
		if(buff != NULL)
			PoolFree(buff);
	}
	return statusCall;			
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//		Logs file creation and/or file opening.
//	Parameters :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566424(v=vs.85).aspx
//	Return value :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff566424(v=vs.85).aspx
//	Process :
//		Copies arguments, handles the non-NULL ObjectAttributes->RootDirectory parameter case (concat
//		of RootDirectory and ObjectName) then logs.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtCreateFile(__out PHANDLE FileHandle, 
							 __in ACCESS_MASK DesiredAccess, 
							 __in POBJECT_ATTRIBUTES ObjectAttributes, 
							 __out PIO_STATUS_BLOCK IoStatusBlock, 
							 __in_opt PLARGE_INTEGER AllocationSize, 
							 __in ULONG FileAttributes, 
							 __in ULONG ShareAccess, 
							 __in ULONG CreateDisposition, 
							 __in ULONG CreateOptions,
							 __in PVOID EaBuffer,
							 __in ULONG EaLength)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId, returnLength;
	ULONG kDesiredAccess, kCreateOptions, kShareAccess;
	USHORT log_lvl = LOG_ERROR;
	UNICODE_STRING full_path;
	PWCHAR parameter = NULL;
	BOOLEAN handle_to_add;
	POBJECT_NAME_INFORMATION nameInformation = NULL;
	HANDLE kRootDirectory, kFileHandle;
	UNICODE_STRING kObjectName;
	
	full_path.Buffer = NULL;
	kObjectName.Buffer = NULL;
	handle_to_add = FALSE;
	
	PAGED_CODE();
	
	currentProcessId = (ULONG)PsGetCurrentProcessId();
	
	kCreateOptions = CreateOptions;
	kDesiredAccess = DesiredAccess;
	
	if((CreateOptions & FILE_DELETE_ON_CLOSE) && (DesiredAccess & DELETE))
	{
		CreateOptions -= FILE_DELETE_ON_CLOSE;
		DesiredAccess -= DELETE;
		if(DesiredAccess == 0)
			DesiredAccess = 1;
		handle_to_add = TRUE;
	}
	
	kShareAccess = ShareAccess;
	ShareAccess |= FILE_SHARE_READ;
	
	statusCall = Orig_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtCreateFile\n");
			
		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));
		kObjectName.Buffer = NULL;
		
		__try
		{

			ProbeForRead(FileHandle, sizeof(HANDLE), 1);
			ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
			ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);
			ProbeForRead(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length, 1);
		
			kFileHandle = *FileHandle;
			kRootDirectory = ObjectAttributes->RootDirectory;
			kObjectName.Length = ObjectAttributes->ObjectName->Length;
			kObjectName.MaximumLength = ObjectAttributes->ObjectName->MaximumLength;
			kObjectName.Buffer = PoolAlloc(kObjectName.MaximumLength);
			RtlCopyUnicodeString(&kObjectName, ObjectAttributes->ObjectName);	
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			exceptionCode = GetExceptionCode();
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,sssssss,FileHandle->ERROR,DesiredAccess->ERROR,FileAttributes->ERROR,CreateDisposition->ERROR,CreateOptions->ERROR,ShareAccess->ERROR,FilePath->ERROR", exceptionCode)))
				sendLogs(currentProcessId, SIG_ntdll_NtCreateFile, parameter);
			else 
				sendLogs(currentProcessId, SIG_ntdll_NtCreateFile, L"0,-1,sssssss,FileHandle->ERROR,DesiredAccess->ERROR,FileAttributes->ERROR,CreateDisposition->ERROR,CreateOptions->ERROR,ShareAccess->ERROR,FilePath->ERROR");
			if(parameter != NULL)
				PoolFree(parameter);
			return statusCall;
		}
	
		if(kRootDirectory)	// handle the not null rootdirectory case
		{
			// allocate both name information struct and unicode string buffer
			nameInformation = PoolAlloc(MAX_SIZE);
			if(nameInformation)
			{
				if(NT_SUCCESS(ZwQueryObject(kRootDirectory, ObjectNameInformation, nameInformation, MAX_SIZE, NULL)))
				{
					full_path.MaximumLength = nameInformation->Name.Length + kObjectName.Length + 2 + sizeof(WCHAR);
					full_path.Buffer = PoolAlloc(full_path.MaximumLength);
					RtlZeroMemory(full_path.Buffer, full_path.MaximumLength);
					RtlCopyUnicodeString(&full_path, &(nameInformation->Name));
					RtlAppendUnicodeToString(&full_path, L"\\");
					RtlAppendUnicodeStringToString(&full_path, &kObjectName);
				}
			}
		}
		else
			RtlInitUnicodeString(&full_path, kObjectName.Buffer);
		
		if(NT_SUCCESS(statusCall))
		{
			// if CreateOptions == FILE_DELETE_ON_CLOSE && DesiredAccess == DELETE), add the handle to the linked list and remove the flags
			if(handle_to_add)
				AddHandleToList(kFileHandle);
			
			log_lvl = LOG_SUCCESS;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,sssssss,FileHandle->0x%08x,DesiredAccess->0x%08x,FileAttributes->0x%x,CreateDisposition->0x%x,CreateOptions->0x%x,ShareAccess->0x%x,FilePath->%wZ", kFileHandle,kDesiredAccess, FileAttributes, CreateDisposition, kCreateOptions,  kShareAccess, &full_path)))
				log_lvl = LOG_PARAM;
		}
		else
		{
			log_lvl = LOG_ERROR;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE,  L"0,%d,sssssss,FileHandle->0x%08x,DesiredAccess->0x%08x,FileAttributes->0x%x,CreateDisposition->0x%x,CreateOptions->0x%x,ShareAccess->0x%x,FilePath->%wZ", statusCall, kFileHandle, kDesiredAccess, FileAttributes, CreateDisposition, kCreateOptions, kShareAccess, &full_path)))
				log_lvl = LOG_PARAM;
		}
		
		switch(log_lvl)
		{
			case LOG_PARAM:
				sendLogs(currentProcessId, SIG_ntdll_NtCreateFile, parameter);
			break;
			case LOG_SUCCESS:
				sendLogs(currentProcessId, SIG_ntdll_NtCreateFile, L"1,0,sssssss,FileHandle->ERROR,DesiredAccess->0,FileAttributes->0,CreateDisposition->0,CreateOptions->0,ShareAccess->0,FilePath->ERROR");
			break;
			default:
				sendLogs(currentProcessId, SIG_ntdll_NtCreateFile, L"0,-1,sssssss,FileHandle->ERROR,DesiredAccess->0,FileAttributes->0,CreateDisposition->0,CreateOptions->0,ShareAccess->0,FilePath->ERROR");
			break;
		}
		if(parameter != NULL)
			PoolFree(parameter);
		if(nameInformation != NULL)
			PoolFree(nameInformation);
	}
	return statusCall;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Description :
//		Logs file modification.
//	Parameters :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff567121(v=vs.85).aspx
//	Return value :
//		See http://msdn.microsoft.com/en-us/library/windows/hardware/ff567121(v=vs.85).aspx
//	Process :
//		logs FileHandle, ByteOffset and Buffer	   
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
NTSTATUS Hooked_NtWriteFile(__in HANDLE FileHandle, 
							__in_opt HANDLE Event, 
							__in_opt PIO_APC_ROUTINE ApcRoutine, 
							__in_opt PVOID ApcContext, 
							__out PIO_STATUS_BLOCK IoStatusBlock, 
							__in PVOID Buffer, 
							__in ULONG Length, 
							__in_opt PLARGE_INTEGER ByteOffset, 
							__in_opt PULONG Key)
{
	NTSTATUS statusCall, exceptionCode;
	ULONG currentProcessId;
	LARGE_INTEGER kByteOffset;
	ULONG_PTR kBufferSize;    
	PUCHAR kBuffer = NULL;
	PWCHAR buff = NULL;
	PWCHAR parameter = NULL; 
	USHORT log_lvl = LOG_ERROR;
	kByteOffset.QuadPart = 0;
	
	PAGED_CODE();

	currentProcessId = (ULONG)PsGetCurrentProcessId();
	statusCall = Orig_NtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
	
	if(IsProcessInList(currentProcessId, pMonitoredProcessListHead) && (ExGetPreviousMode() != KernelMode))
	{
		Dbg("Call NtWriteFile\n");

		parameter = PoolAlloc(MAX_SIZE * sizeof(WCHAR));

		__try
		{
			ProbeForRead(IoStatusBlock, sizeof(IO_STATUS_BLOCK), 1);
			ProbeForRead((PVOID)IoStatusBlock->Information, sizeof(ULONG), 1);
			if(ByteOffset)
				kByteOffset = ProbeForReadLargeInteger(ByteOffset);
			kBufferSize = IoStatusBlock->Information;
			ProbeForRead(Buffer, kBufferSize, 1);
			kBuffer = Buffer;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			exceptionCode = GetExceptionCode();
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"0,%d,sss,FileHandle->0,buffer->ERROR,offset->0", exceptionCode)))
				sendLogs(currentProcessId, SIG_ntdll_NtWriteFile, parameter);
			else
				sendLogs(currentProcessId, SIG_ntdll_NtWriteFile, L"0,-1,sss,FileHandle->0,buffer->ERROR,offset->0");
			if(parameter != NULL)
				PoolFree(parameter);
			return statusCall;
		}

		// log buffer
		buff = PoolAlloc(BUFFER_LOG_MAX);
		CopyBuffer(buff, kBuffer, kBufferSize);
	
		if(NT_SUCCESS(statusCall))
		{
			log_lvl = LOG_SUCCESS;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE, L"1,0,sss,FileHandle->0x%08x,buffer->%ws,offset->%d", FileHandle, buff, kByteOffset.QuadPart)))
				log_lvl = LOG_PARAM;
		}
		else
		{
			log_lvl = LOG_ERROR;
			if(parameter && NT_SUCCESS(RtlStringCchPrintfW(parameter, MAX_SIZE,  L"0,%d,sss,FileHandle->0x%08x,buffer->%ws,offset->%d", statusCall, FileHandle, buff, kByteOffset.QuadPart)))
				log_lvl = LOG_PARAM;
		}
			
		switch(log_lvl)
		{
			case LOG_PARAM:
				sendLogs(currentProcessId, SIG_ntdll_NtWriteFile, parameter);
				break;
			case LOG_SUCCESS:
				sendLogs(currentProcessId, SIG_ntdll_NtWriteFile, L"1,0,sss,FileHandle->0,buffer->ERROR,offset->0");
				break;
			default:
				sendLogs(currentProcessId, SIG_ntdll_NtWriteFile, L"0,-1,sss,FileHandle->0,buffer->ERROR,offset->0");
		}

		if(buff != NULL)
			PoolFree(buff);
		if(parameter != NULL)
			PoolFree(parameter);
	}
	return statusCall;
}
