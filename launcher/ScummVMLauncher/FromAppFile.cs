using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace ScummVMLauncher
{
    /// <summary>
    /// File helpers for arbitrary paths (E:\) inside a UWP AppContainer.
    /// Uses the *FromApp API family, which succeeds when the package has
    /// broadFileSystemAccess. Plain System.IO is denied for non-granted
    /// paths in AppContainer, so all E:\ I/O goes through these.
    ///
    /// Pattern mirrors x-files-uwp (Win32FileStream / Win32FileWriteStream /
    /// FileOperations): raw Win32 handles + ReadFile/WriteFile P/Invoke.
    /// System.IO.FileStream must NOT be used here — it does not work in the
    /// UWP sandbox on Xbox.
    /// </summary>
    internal static class FromAppFile
    {
        private const uint GENERIC_READ = 0x80000000;
        private const uint GENERIC_WRITE = 0x40000000;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint FILE_SHARE_DELETE = 0x00000004;
        private const uint CREATE_ALWAYS = 2;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_ATTRIBUTE_NORMAL = 0x80;
        private const uint FILE_FLAG_SEQUENTIAL_SCAN = 0x08000000;
        private const uint FILE_ATTRIBUTE_DIRECTORY = 0x10;
        private const int GetFileExInfoStandard = 0;
        private const uint INVALID_HANDLE = 0xFFFFFFFF;
        private const int ERROR_ALREADY_EXISTS = 183;
        private const int ERROR_FILE_EXISTS = 80;
        private const int ERROR_FILE_NOT_FOUND = 2;
        private const int ERROR_PATH_NOT_FOUND = 3;
        private const int ERROR_ACCESS_DENIED = 5;

        private const uint FILE_BEGIN = 0;
        private const uint FILE_CURRENT = 1;
        private const uint FILE_END = 2;

        [StructLayout(LayoutKind.Sequential)]
        private struct WIN32_FILE_ATTRIBUTE_DATA
        {
            public uint dwFileAttributes;
            public long ftCreationTime;
            public long ftLastAccessTime;
            public long ftLastWriteTime;
            public uint nFileSizeHigh;
            public uint nFileSizeLow;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WIN32_FIND_DATA
        {
            public uint dwFileAttributes;
            public long ftCreationTime;
            public long ftLastAccessTime;
            public long ftLastWriteTime;
            public uint nFileSizeHigh;
            public uint nFileSizeLow;
            public uint dwReserved0;
            public uint dwReserved1;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string cFileName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 14)]
            public string cAlternateFileName;
        }

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFileFromAppW(
            string lpFileName, uint dwDesiredAccess, uint dwShareMode,
            IntPtr lpSecurityAttributes, uint dwCreationDisposition,
            uint dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateDirectoryFromAppW(string lpPathName, IntPtr lpSecurityAttributes);

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool GetFileAttributesExFromAppW(
            string lpFileName, int fInfoLevelId, out WIN32_FILE_ATTRIBUTE_DATA lpFileInformation);

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CopyFileFromAppW(
            string lpExistingFileName, string lpNewFileName, bool bFailIfExists);

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool DeleteFileFromAppW(string lpFileName);

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool RemoveDirectoryFromAppW(string lpPathName);

        [DllImport("api-ms-win-core-file-fromapp-l1-1-0.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr FindFirstFileExFromAppW(
            string lpFileName, int fInfoLevelId, out WIN32_FIND_DATA lpFindFileData,
            int fSearchOp, IntPtr lpSearchFilter, uint dwAdditionalFlags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool FindNextFileW(IntPtr hFindFile, out WIN32_FIND_DATA lpFindFileData);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FindClose(IntPtr hFindFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool ReadFile(
            IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToRead,
            out uint lpNumberOfBytesRead, IntPtr lpOverlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteFile(
            IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToWrite,
            out uint lpNumberOfBytesWritten, IntPtr lpOverlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileSizeEx(IntPtr hFile, out long lpFileSize);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFilePointerEx(
            IntPtr hFile, long lDistanceToMove, out long lpNewFilePointer, uint dwMoveMethod);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr hObject);

        // Plain Win32 call; allowed from AppContainer.
        [DllImport("kernel32.dll")]
        private static extern uint GetLogicalDrives();

        private static IntPtr InvalidHandle { get { return new IntPtr(-1); } }

        // ─────────────────────────────────────────────────────────────────────────
        //  Public helpers
        // ─────────────────────────────────────────────────────────────────────────

        public static bool DriveExists(char letter)
        {
            int idx = char.ToUpperInvariant(letter) - 'A';
            if (idx < 0 || idx > 25)
                return false;
            uint mask = GetLogicalDrives();
            return (mask & (1u << idx)) != 0;
        }

        /// <summary>Space-separated list of present drive letters, e.g. "C D E G". Diagnostic helper.</summary>
        public static string DriveList()
        {
            uint mask = GetLogicalDrives();
            var sb = new System.Text.StringBuilder();
            for (int i = 0; i < 26; i++)
            {
                if ((mask & (1u << i)) != 0)
                {
                    if (sb.Length > 0)
                        sb.Append(' ');
                    sb.Append((char)('A' + i));
                }
            }
            return sb.Length == 0 ? "(none)" : sb.ToString();
        }

        public static bool Exists(string path)
        {
            if (string.IsNullOrEmpty(path))
                return false;
            return GetFileAttributesExFromAppW(path, GetFileExInfoStandard, out WIN32_FILE_ATTRIBUTE_DATA data);
        }

        public static bool IsDirectory(string path)
        {
            if (!GetFileAttributesExFromAppW(path, GetFileExInfoStandard, out WIN32_FILE_ATTRIBUTE_DATA data))
                return false;
            return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        /// <summary>Recursively creates a directory (and parents) on an arbitrary path.</summary>
        public static void CreateDirectory(string path)
        {
            if (Exists(path))
                return;
            string parent = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(parent) && !Exists(parent))
                CreateDirectory(parent);
            if (!CreateDirectoryFromAppW(path, IntPtr.Zero))
            {
                int err = Marshal.GetLastWin32Error();
                if (err != ERROR_ALREADY_EXISTS && !Exists(path))
                    throw new IOException("CreateDirectoryFromAppW failed (" + err + "): " + path);
            }
        }

        /// <summary>Opens an existing file for read on an arbitrary path.</summary>
        public static Stream OpenRead(string path)
        {
            IntPtr handle = CreateFileFromAppW(
                path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                IntPtr.Zero, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new IOException("CreateFileFromAppW open-for-read failed (" + Marshal.GetLastWin32Error() + "): " + path);
            return new Win32ReadStream(handle, path);
        }

        /// <summary>Creates/truncates a file for write on an arbitrary path.</summary>
        public static Stream OpenWrite(string path)
        {
            IntPtr handle = CreateFileFromAppW(
                path, GENERIC_WRITE,
                FILE_SHARE_READ,
                IntPtr.Zero, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new IOException("CreateFileFromAppW open-for-write failed (" + Marshal.GetLastWin32Error() + "): " + path);
            return new Win32WriteStream(handle, path);
        }

        public static void WriteAllText(string path, string content)
        {
            byte[] bytes = new UTF8Encoding(false).GetBytes(content);
            using (Stream s = OpenWrite(path))
                s.Write(bytes, 0, bytes.Length);
        }

        public static string ReadAllText(string path)
        {
            using (Stream s = OpenRead(path))
            using (StreamReader r = new StreamReader(s, Encoding.UTF8, true))
                return r.ReadToEnd();
        }

        /// <summary>Copies the content of a System.IO stream (source) to an arbitrary path (dest).</summary>
        public static void WriteFromStream(string dest, Stream source)
        {
            byte[] buffer = new byte[256 * 1024];
            using (Stream s = OpenWrite(dest))
            {
                int n;
                while ((n = source.Read(buffer, 0, buffer.Length)) > 0)
                    s.Write(buffer, 0, n);
            }
        }

        /// <summary>Copies a file between arbitrary paths (both sides go through FromApp).</summary>
        public static void Copy(string src, string dst, bool overwrite)
        {
            if (!CopyFileFromAppW(src, dst, !overwrite))
            {
                int err = Marshal.GetLastWin32Error();
                if (!overwrite && err == ERROR_FILE_EXISTS)
                    return;
                throw new IOException("CopyFileFromAppW failed (" + err + "): " + dst);
            }
        }

        public static void Delete(string path)
        {
            if (!DeleteFileFromAppW(path))
                throw new IOException("DeleteFileFromAppW failed (" + Marshal.GetLastWin32Error() + "): " + path);
        }

        /// <summary>Recursively deletes a directory tree on an arbitrary path.</summary>
        public static void DeleteTree(string dir)
        {
            DeleteTreeRecursive(dir);
            if (Exists(dir) && !RemoveDirectoryFromAppW(dir))
                throw new IOException("RemoveDirectoryFromAppW failed (" + Marshal.GetLastWin32Error() + "): " + dir);
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  Internals
        // ─────────────────────────────────────────────────────────────────────────

        private static void DeleteTreeRecursive(string dir)
        {
            WIN32_FIND_DATA findData = new WIN32_FIND_DATA();
            IntPtr hFind = FindFirstFileExFromAppW(
                dir + "\\*", GetFileExInfoStandard, out findData, 0, IntPtr.Zero, 0);
            if (hFind == InvalidHandle)
            {
                int err = Marshal.GetLastWin32Error();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
                    throw new IOException("FindFirstFileExFromAppW failed (" + err + "): " + dir);
                return;
            }

            try
            {
                do
                {
                    if (findData.cFileName == "." || findData.cFileName == "..")
                        continue;
                    string full = dir + "\\" + findData.cFileName;
                    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    {
                        DeleteTreeRecursive(full);
                        RemoveDirectoryFromAppW(full);
                    }
                    else
                    {
                        DeleteFileFromAppW(full);
                    }
                }
                while (FindNextFileW(hFind, out findData));
            }
            finally
            {
                FindClose(hFind);
            }
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  Raw streams (no System.IO.FileStream — unsafe in UWP sandbox on Xbox)
        // ─────────────────────────────────────────────────────────────────────────

        private sealed class Win32ReadStream : Stream
        {
            private readonly IntPtr _handle;
            private readonly string _path;
            private readonly long _length;
            private long _position;
            private bool _disposed;

            public Win32ReadStream(IntPtr handle, string path)
            {
                _handle = handle;
                _path = path;
                _position = 0;
                if (!GetFileSizeEx(handle, out _length))
                    _length = 0;
            }

            public override bool CanRead { get { return !_disposed; } }
            public override bool CanSeek { get { return !_disposed; } }
            public override bool CanWrite { get { return false; } }
            public override long Length { get { return _length; } }
            public override long Position
            {
                get { return _position; }
                set { Seek(value, SeekOrigin.Begin); }
            }

            public override int Read(byte[] buffer, int offset, int count)
            {
                if (_disposed)
                    throw new ObjectDisposedException("Win32ReadStream");
                if (count == 0)
                    return 0;

                byte[] readBuf = buffer;
                byte[] copyBuf = null;
                if (offset != 0)
                {
                    copyBuf = new byte[count];
                    readBuf = copyBuf;
                }

                uint bytesRead;
                bool ok = ReadFile(_handle, readBuf, (uint)count, out bytesRead, IntPtr.Zero);
                if (!ok)
                    throw new IOException("ReadFile failed (" + Marshal.GetLastWin32Error() + "): " + _path);
                if (bytesRead == 0)
                    return 0;
                if (offset != 0)
                    Array.Copy(readBuf, 0, buffer, offset, (int)bytesRead);
                _position += bytesRead;
                return (int)bytesRead;
            }

            public override long Seek(long offset, SeekOrigin origin)
            {
                if (_disposed)
                    throw new ObjectDisposedException("Win32ReadStream");
                uint method = origin == SeekOrigin.Begin ? FILE_BEGIN : origin == SeekOrigin.Current ? FILE_CURRENT : FILE_END;
                long newPos;
                if (!SetFilePointerEx(_handle, offset, out newPos, method))
                    throw new IOException("SetFilePointerEx failed (" + Marshal.GetLastWin32Error() + "): " + _path);
                _position = newPos;
                return _position;
            }

            public override void Flush() { }
            public override void SetLength(long value) { throw new NotSupportedException(); }
            public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }

            protected override void Dispose(bool disposing)
            {
                if (!_disposed)
                {
                    CloseHandle(_handle);
                    _disposed = true;
                }
                base.Dispose(disposing);
            }
        }

        private sealed class Win32WriteStream : Stream
        {
            private readonly IntPtr _handle;
            private readonly string _path;
            private long _position;
            private bool _disposed;

            public Win32WriteStream(IntPtr handle, string path)
            {
                _handle = handle;
                _path = path;
                _position = 0;
            }

            public override bool CanRead { get { return false; } }
            public override bool CanSeek { get { return !_disposed; } }
            public override bool CanWrite { get { return !_disposed; } }
            public override long Length { get { throw new NotSupportedException(); } }
            public override long Position
            {
                get { return _position; }
                set { Seek(value, SeekOrigin.Begin); }
            }

            public override void Write(byte[] buffer, int offset, int count)
            {
                if (_disposed)
                    throw new ObjectDisposedException("Win32WriteStream");
                if (count == 0)
                    return;

                byte[] writeBuf = buffer;
                byte[] copyBuf = null;
                if (offset != 0)
                {
                    copyBuf = new byte[count];
                    Array.Copy(buffer, offset, copyBuf, 0, count);
                    writeBuf = copyBuf;
                }

                int remaining = count;
                while (remaining > 0)
                {
                    uint bytesWritten;
                    bool ok = WriteFile(_handle, writeBuf, (uint)remaining, out bytesWritten, IntPtr.Zero);
                    if (!ok)
                        throw new IOException("WriteFile failed, " + remaining + " bytes pending (" + Marshal.GetLastWin32Error() + "): " + _path);
                    if (bytesWritten == 0)
                        throw new IOException("WriteFile wrote 0 bytes: " + _path);
                    remaining -= (int)bytesWritten;
                    if (remaining > 0)
                        Array.Copy(writeBuf, (int)bytesWritten, writeBuf, 0, remaining);
                }
                _position += count;
            }

            public override long Seek(long offset, SeekOrigin origin)
            {
                if (_disposed)
                    throw new ObjectDisposedException("Win32WriteStream");
                uint method = origin == SeekOrigin.Begin ? FILE_BEGIN : origin == SeekOrigin.Current ? FILE_CURRENT : FILE_END;
                long newPos;
                if (!SetFilePointerEx(_handle, offset, out newPos, method))
                    throw new IOException("SetFilePointerEx failed (" + Marshal.GetLastWin32Error() + "): " + _path);
                _position = newPos;
                return _position;
            }

            public override void Flush() { }
            public override int Read(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
            public override void SetLength(long value) { throw new NotSupportedException(); }

            protected override void Dispose(bool disposing)
            {
                if (!_disposed)
                {
                    CloseHandle(_handle);
                    _disposed = true;
                }
                base.Dispose(disposing);
            }
        }
    }
}
