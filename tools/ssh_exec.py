import sys, os, stat
import paramiko

def connect(host, user, pw):
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(hostname=host, username=user, password=pw, timeout=float(os.environ.get("SSH_TIMEOUT","20")),
              banner_timeout=30, auth_timeout=30, look_for_keys=False, allow_agent=False)
    return c

def run(c, cmd, outfile):
    _in, out, err = c.exec_command(cmd, timeout=3600)
    o = out.read().decode("utf-8","replace"); e = err.read().decode("utf-8","replace")
    rc = out.channel.recv_exit_status()
    text = o + (("\n[stderr]\n"+e) if e else "") + ("\n[exit %d]\n" % rc)
    if outfile:
        open(outfile,"w",encoding="utf-8",newline="\n").write(text)
        print("WROTE %s (%d chars) exit=%d" % (outfile, len(text), rc))
    else:
        sys.stdout.write(text)
    return rc

def main():
    if len(sys.argv) < 5:
        print("usage: ssh_exec.py HOST USER PASS [--out F] -- cmd | --put L R | --get R L | --script L"); return 2
    host, user, pw = sys.argv[1], sys.argv[2], sys.argv[3]
    rest = sys.argv[4:]; outfile = None
    if rest and rest[0] == "--out": outfile = rest[1]; rest = rest[2:]
    if rest and rest[0] == "--": rest = rest[1:]
    c = connect(host, user, pw)
    try:
        if rest[0] == "--put":
            s=c.open_sftp(); s.put(rest[1], rest[2]); s.close(); print("OK put"); return 0
        if rest[0] == "--get":
            s=c.open_sftp(); s.get(rest[1], rest[2]); s.close(); print("OK get"); return 0
        if rest[0] == "--script":
            local = rest[1]; remote = "/tmp/dsh_" + os.path.basename(local)
            s=c.open_sftp(); s.put(local, remote); s.close()
            return run(c, "bash " + remote, outfile)
        return run(c, " ".join(rest), outfile)
    finally:
        c.close()

if __name__ == "__main__":
    sys.exit(main())