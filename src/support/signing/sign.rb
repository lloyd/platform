#!/usr/bin/env ruby

require 'rbconfig'
require 'fileutils'
include Config

# Create signing certificates and sign/verify files.  
# This script is intended for use by the BrowserPlus team.
# Even though this script can do it, service signing should
# be done using the ServicePublisher tool, which takes care
# of all packaging and publishing.  This scripts true use is
# to generate certificates and sign/verify Windows executables
# via signtool.  For testing, it can also be useful to validate
# and unpack signed services.

# Service developers should use ServicePublisher in the sdk.

# On windows, if signing/verifying a .exe, .dll, or .cab then
# microsoft's signtool is used.  Otherwise, openssl is used.
# In openssl case, results should be identical to using the
# bp::sign methods

# to make microsoft pfx file from Yahoo! supplied pvk and spc, cd to authenticode and:
#  pvk2pfx -pvk yahoo.pvk -spc yahoo.spc -pfx yahoo.pfx -pi PASSWORD -po PASSWORD -f
# where PASSWORD is the secret password
# 

def usage()
    puts "Usage: #{File.basename($0)} makeCerts -certType=devel|prod -outDir=<dir> [-password=<pwd>]"
    puts "       #{File.basename($0)} sign -certType=devel|prod [-password=<pwd>] [-authKey=<pfxPath>] [-authenticodePassword=<pwd2>] <files>"
    puts "       #{File.basename($0)} verify [-certStore=<path>] <files>"
    exit 1
end

# moral equivalent of "which"
def which(cmd)
    envSep = $platform == "Darwin" ? ':' : ';'
    ENV['PATH'].split(envSep).each do |dir|
        candidate = File.join(dir, cmd)
        # can't use executable?, it doesn't work on the mighty Vista
        return candidate if File.exists?(candidate)
    end
    return nil
end

def runCmd(cmd)
    rval = system(cmd)
    return rval
end

usage() if ARGV.length < 1 || ARGV[1] == "help"


# Verify needed environment vars
["BP_PLATFORM_PATH", "BP_INTERNAL_SDK_PATH"].each do |p|
    if !ENV.has_key?(p)
        puts "#{p} environment variable not set!"
        exit -1
    end
    path = ENV[p]
    if !File.exist?(path)
      puts "#{p} (#{path}) does not exist!"
        exit -1
    end
end

topDir = File.dirname(File.expand_path(__FILE__))
platform = CONFIG['arch'] =~ /mswin|mingw/ ? "Windows" : "Darwin"
exeSuffix = platform == "Windows" ? ".exe" : ""
signedBy = "BrowserPlus"
timeurl = "http://timestamp.verisign.com/scripts/timstamp.dll"

certType = nil
password = nil
authKey = nil
authenticodePassword = nil
passIn = nil
passOut = nil
certStore = nil
publicKey = nil
privateKey = nil
outDir = nil

bpsigner = File.join(ENV["BP_INTERNAL_SDK_PATH"], "signing", "bpsigner#{exeSuffix}")
if !File.exist?(bpsigner) 
    puts "#{bpsigner} not found, script assumes that 'build' is a sibling of 'src'"
    exit -1
end

# parse command line
fileStart = 2
ARGV[1..ARGV.length].each do |arg|
    v = arg.split('=')
    if v.length == 2
        case v[0]
        when '-certType'
            certType = v[1]
        when '-password'
            password = v[1]
            fileStart = fileStart + 1
        when '-authKey'
            authKey = v[1]
            fileStart = fileStart + 1
        when '-authenticodePassword'
            authenticodePassword = v[1]
            fileStart = fileStart + 1
        when '-certStore'
            certStore = v[1]
        when '-outDir'
            outDir = v[1]
        else
            usage()
        end
    end
end

if certType != nil
    publicKey = "#{topDir}/#{certType}/#{signedBy}.crt"
    privateKey = "#{topDir}/#{certType}/#{signedBy}.pvk"
end

opensslPath = File.join(ENV["BP_PLATFORM_PATH"], "external", 
                        "dist", "bin", "openssl#{exeSuffix}")
if !File.exist?(opensslPath) 
    puts "#{opensslPath} not found"
    exit -1
end
ENV["OPENSSL_CONF"] = File.join(ENV["BP_PLATFORM_PATH"], "external", 
                                "dist", "ssl", "openssl.cnf")

curdir = Dir.getwd
Dir.chdir(topDir) do 
    case ARGV[0]
    when "makeCerts" 
        usage() if certType == nil
        usage() if outDir == nil
        if password
            passIn = "-passin pass:#{password}"
            passOut = "-passout pass:#{password}"
        end
        FileUtils.mkdir_p("#{outDir}/#{certType}")
        puts "Making BrowserPlus certificate"
        # make private key
        runCmd("#{opensslPath} genrsa #{passOut} -des3 -out #{outDir}/#{certType}/#{signedBy}.pvk 4096") 
        # make public key (certificate)
        runCmd("#{opensslPath} req #{passIn} -new -x509 -days 5000 -key #{outDir}/#{certType}/#{signedBy}.pvk -out #{outDir}/#{certType}/#{signedBy}.crt")

    when "sign"
        usage() if certType == nil
        usage() if fileStart > ARGV.length 
        if certType == "devel"
            # password for development certs only!
            password = "FreeYourBrowser" if password == nil
            authenticodePassword = "FreeYourBrowser" if authenticodePassword == nil
        end
        ARGV[fileStart..ARGV.length].each do |arg|
            thisFile = File.expand_path(arg, curdir)
            FileUtils.chmod(0755, thisFile)
            if platform == "Windows" && (thisFile =~/.exe$/ || thisFile =~ /.dll$/ || thisFile =~ /.cab$/)
                if which('signtool.exe') == nil
                    puts "signtool not in path, run vs8 vsvars.bat"
                    exit(-1)
                end
                if authenticodePassword == nil
                    STDOUT.print("Authenticode signing password: ")
                    authenticodePassword = STDIN.gets.chomp
                end
                if !runCmd("signtool sign /p #{authenticodePassword} /f \"#{authKey}\" /t #{timeurl} /v #{thisFile}")
                    puts "Signing #{thisFile} failed."
                    exit(-1)
                end
            else
                if password == nil
                    STDOUT.print("BrowserPlus signing password: ")
                    password = STDIN.gets.chomp
                end
                runCmd("#{bpsigner} sign -privateKey=#{privateKey} -publicKey=#{publicKey} -in=#{thisFile} -out=#{thisFile}.sig -password=#{password}")
            end
        end

    when "verify"
        usage() if fileStart > ARGV.length 
        ARGV[1..ARGV.length].each do |arg|
            thisFile = File.expand_path(arg, curdir)
            if platform == "Windows" && (thisFile =~/.exe$/ || thisFile =~ /.dll$/ || thisFile =~ /.cab$/)
                if which('signtool.exe') == nil
                    puts "signtool not in path, run vs8 vsvars.bat"
                    exit(-1)
                end
                if !runCmd("signtool verify /a /v #{thisFile}")
                    puts "Verifying #{thisFile} failed."
                    exit(-1)
                end
            else
                cmd = "#{bpsigner} verify -in=#{thisFile} -signature=#{thisFile}.sig"
                cmd += " -certStore=#{certStore}" if certStore != nil
                runCmd(cmd)
            end
        end

    else
        usage()
    end
end
