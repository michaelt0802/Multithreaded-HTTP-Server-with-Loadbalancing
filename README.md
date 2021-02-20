To run the Server, first open up the httpservers that will be used by the loadbalancer. Next run the loadbalancer with the port for the loadbalancer followed by the ports of the httpservers.
Flag -R will be for the amount of requests required to trigger a preemtively healthcheck on the servers. Flag -N will be the amount of concurrent requests the loadbalancer can handle at any one time.

KNOWN ISSUES:
I was having issues having GET commands work on large binary files for a while. Hopefully this bug doesn't crop up again.
