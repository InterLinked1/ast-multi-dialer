# ast-multi-dialer

This is a simple CLI based dialer that uses AMI (Asterisk Manager Interface) to manipulate virtual "lines" remotely. A typical setup will look like this:

- Server under testing, with lines provisioned using PJSIP (or SIP)
- Server for testing, with line registrations using PJSIP

This isn't really so much a dialer per se as a line manipulator, useful for testing. There is no audio output, for instance, so this is *not* a generic softphone program. You can use brief commands that are somewhat similar to the Hayes command set.

The main application of this is performing (potentially complex) testing that requires access to multiple telephone lines, without the tester having to physically manipulate multiple telephones. It may also be used as part of an automated testing strategy.

Currently, this program is very basic. The only configurable settings are provided above, and this is only set up to work with PJSIP locally (though the server could use PJSIP or SIP).

## Supported Functionality

This program is mainly intended for testing analog lines, using a SIP interface. Run `./astmultidialer -?` for program usage and, during a session, press `?` for all available options.

The following is currently supported:

- Go off-hook
- Go on-hook
- Send hook flash
- Send DTMF digits

That's about it. See the note below.

You can also do other simple things that aren't line-related, like sleep for a given period of time, useful if you are scripting the actions (which you can feed in by redirecting to STDIN).

### What can I do with this program?

- You can do pretty much anything you could with a standard 2500 telephone set (or rather, 9 standard 2500 sets). That is, you can originate calls (by going off-hook), dialing DTMF digits, etc.
  - You can make three-way conference calls by hook flashing, etc.
  - You could make an outbound call, receive a call waiting, and answer it by flashing, etc.

Basically, think of this program as providing you with nine virtual 2500 sets, except the handset doesn't have a microphone or a speaker (no sound I/O).

### What can I not do with this program?

- There is no audio or video support (hence, this is *not* a softphone)

- You cannot "register" to a SIP extension. All the control is done using AMI.

- The dialer only does things to the line, it doesn't let you know what's happening on it.

## Compiling

This program requires being dynamically linked with [CAMI](https://github.com/InterLinked1/cami). You will need to first ensure this is built and installed on your system.

Afterwards, you can simply run `make`.

Before you compile, you should update these macros at the top of the file for your dialplan:

- `PEER_PREFIX`
- `PLAR_CODE`
- `PLAR_DIALPLAN_CONTEXT`
- `PLAR_DIALPLAN_EXTEN`

Essentially, when the "off-hook" command is used, it will place a call to `PJSIP/$PLAR_CODE@$PEER_PREFIX$X`, where `X` is the line number.

The call will be connected locally in the dialplan to `PLAR_DIALPLAN_CONTEXT`,`PLAR_DIALPLAN_EXTEN`,1 (so make sure this location exists).
It should probably be something like this:

```
[idle]
exten => _X!,1,Answer()
	same => n,Wait(${EXTEN})
	same => n,Hangup()
```

The reason it doesn't dial an application directly is it needs to answer, so that the origination will stay up forever, rather than return after 30 seconds.

I'm not aware of an application in a standard Asterisk install that does what's needed here, so you'll need to add your own context like the above to your dialplan.

## Notes

- The answer function (`a` command) is not currently implemented.

- This program is not being actively developed. It is really intended for being able to quickly and easily originate multiple test calls from a terminal, as opposed to having to use multiple physical telephones, nothing more, nothing less. If you require more functionality than this, you probably need something more sophisticated. That said, PRs are certainly welcome.
