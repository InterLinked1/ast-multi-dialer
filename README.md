# ast-multi-dialer

This is a simple CLI based dialer that uses AMI (Asterisk Manager Interface) to manipulate virtual "lines" remotely. A typical setup will look like this:

- Server under testing, with lines provisioned using PJSIP (or SIP)
- Server for testing, with line registrations using PJSIP

This isn't really so much a dialer per se as a line manipulator, useful for testing. There is no audio output, for instance, so this is *not* a generic softphone program. You can use brief commands that are somewhat similar to the Hayes command set.

The main application of this is performing (potentially complex) testing that requires access to multiple telephone lines, without the tester having to physically manipulate multiple telephones. It may also be used as part of an automated testing strategy.

Currently, this program is very basic. The only configurable settings are provided above, and this is only set up to work with PJSIP locally (though the server could use PJSIP or SIP).

## Compiling

This program requires being statically linked with [CAMI](https://github.com/InterLinked1/cami). This is done automatically if you run `make`.

## Notes

- The answer function is not currently implemented.

- This program is not being actively developed. It is really intended for being able to quickly and easily originate multiple test calls from a terminal, as opposed to having to use multiple physical telephones, nothing more, nothing less. If you require more functionality than this, you probably need something more sophisticated. That said, PRs are certainly welcome.
