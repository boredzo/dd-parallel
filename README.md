# dd-parallel
## A fast parallelized block copier

When you want to duplicate an entire drive, you use a block copier like dd that's capable of reading and writing device files and that specializes in streaming data from one to the other.

dd (at least the implementation that comes with macOS) is implemented as a sequential read-write loop: read, then write, then read, then write, and so on. While this is simple, it is inefficient—the time that dd spends reading is time that could also be spent writing.

dd-parallel is a new block copier that uses GCD (macOS version) or threads (POSIX version) to do reading and writing at the same time in order to deliver faster throughput than the system dd.

## How much faster is it?

On a 2018 MacBook Pro running Catalina, copying between two Seagate 2.5-inch USB 3.0 hard disk drives, the approximate speeds at the start of a copy are:

- system dd: 80 MB/sec
- dd-parallel: 140 MB/sec

Note that “at the start of a copy” matters because these results were obtained with hard disk drives. Hard drive throughput declines over time from the start of the copy toward the end; I theorize that this is because the drive is progressing from the outside of the platters toward the inside, where track circumference is shorter and thus seeks happen more often, dragging down throughput. (This is apparently [quite normal](https://www.tomshardware.com/reviews/understanding-hard-drive-performance,1557-9.html).)

The theoretical maximum of USB 3.0 is 5 Gbps, which is 625 MB/sec; I suspect my hard drives are operating at the limit of a hard drive, not reaching the limits of USB 3. If you try this between two separate SSDs, please let me know the details (makes, models, interface) and the results.

I've tried two inexpensive 512 GB Inland SSDs from Micro Center. On my 2018 Mac mini running Monterey, with the SSDs plugged directly into the Thunderbolt ports:

- system dd: 370 MiB/sec 
- dd-parallel: 720 MiB/sec

dd-parallel drives these particular SSDs so hard that they overheat and throttle to roughly 100 MiB/sec.

## How do I use it?

Despite the name, I've chosen not to bother reimplementing dd's interface, neither in terms of arguments consumed nor output presented.

The usage is:

	dd-parallel in-file out-file
	
in-file and out-file are normally device files such as `/dev/rdisk1`.

macOS note: I recommend always using `rdisk1` rather than `disk1` when pointing dd-parallel (or dd for that matter) at such files, because `disk1` has a kernel buffer in front of it that severely diminishes performance. (It's not meant for this use case.) `rdisk1` accesses the device more directly.

**BE VERY CAREFUL WHICH PATHS YOU GIVE IT.** If you are not ABSOLUTELY SURE you've got the right paths, don't use it. Like dd, this is an ion cannon that can and will destroy your data if you point it in the wrong direction.

[Currently macOS only] There is one option, `--md5`. This is a self-test that verifies that dd-parallel is writing what it should be. It is *not* a verification of the bits on disk. Feel free to use it to test that dd-parallel is not mixing up data (particularly if you make any changes to the source code that affect the parallelism), but don't expect it to verify writes—it does not do that.

On macOS, while the copy is in progress, you can send it a SIGINFO signal by pressing ctrl-T. This will cause it to write out a report of how much data it has written and how fast it's going. The format for this is not final but is definitely not going to match dd. On Linux, SIGUSR1 will achieve the same result; you'll have to send it using kill or killall manually, since Linux has no equivalent to ctrl-T.

dd-parallel will also write out a similar report when it finishes.

## How do I compile it?

On macOS, open the Xcode project and build the dd-parallel scheme. Or, use `xcodebuild -scheme dd-parallel`.

On Linux, you will need to install `libbsd-dev`. Then, `./configure` followed by `make` should do the right thing. (This also works on macOS. It will build the POSIX threads implementation rather than the Cocoa implementation.)

This code hasn't been tested on other platforms. I've tested it on macOS and Linux and nothing else; if you want to add support for something else, such as BSD, you'll need to add a new prefix header in the dd-parallel-posix directory, and possibly make additions to `configure`. (I don't use autoconf, so this is a hand-written `configure` that should be much easier to directly edit.) Please submit a patch.
