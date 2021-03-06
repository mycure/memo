#pragma once

#include <elle/das/Symbol.hh>

#include <memo/utility.hh>

#ifdef ELLE_WINDOWS
# undef stat
#endif

namespace memo
{
  namespace cli
  {
    // Symbol, short option char [0], help string, whether positional argument [false].

    ELLE_DAS_CLI_SYMBOL(account, "cloud service account name");
    ELLE_DAS_CLI_SYMBOL(add, "add users, administrators and groups to group (prefix: @<group>, ^<admin>");
    ELLE_DAS_CLI_SYMBOL(add_admin, "add administrator to group");
    ELLE_DAS_CLI_SYMBOL(add_group, "add group to group");
    ELLE_DAS_CLI_SYMBOL(add_user, "add user to group");
    ELLE_DAS_CLI_SYMBOL(admin_r, "set admin users that can read all data");
    ELLE_DAS_CLI_SYMBOL(admin_remove, "remove given users from all admin lists (prefix: @<group>, requires mountpoint)");
    ELLE_DAS_CLI_SYMBOL(admin_rw, "set admin users that can read and write all data");
    ELLE_DAS_CLI_SYMBOL(advertise_host, "advertise extra endpoint using given host");
    ELLE_DAS_CLI_SYMBOL(all, "all information");
    ELLE_DAS_CLI_SYMBOL(allow_create_passport, "allow user to create passports for network");
    ELLE_DAS_CLI_SYMBOL(allow_root_creation, "create the filesystem root if not found");
    ELLE_DAS_CLI_SYMBOL(as, 'a', "user to run commands as");
    ELLE_DAS_CLI_SYMBOL(async, "use asynchronous write operations");
    ELLE_DAS_CLI_SYMBOL(avatar, "path to an image to use as avatar");
    ELLE_DAS_CLI_SYMBOL(aws, "Amazon Web Services (or S3 compatible) credentials");
    ELLE_DAS_CLI_SYMBOL(block_size, "{object} block size");
    ELLE_DAS_CLI_SYMBOL(bucket, "bucket name");
    ELLE_DAS_CLI_SYMBOL(cache, "enable caching with default values");
    ELLE_DAS_CLI_SYMBOL(cache_disk_size, "size of disk cache for immutable data in bytes (default: 512MB)");
    ELLE_DAS_CLI_SYMBOL(cache_ram_invalidation, "RAM block cache invalidation time in seconds (default: 15s)");
    ELLE_DAS_CLI_SYMBOL(cache_ram_size, "maximum RAM block cache size in bytes (default: 64MB)");
    ELLE_DAS_CLI_SYMBOL(cache_ram_ttl, "RAM block cache time-to-live in seconds (default: 5min)");
    ELLE_DAS_CLI_SYMBOL(capacity, 'c', "limit silo capacity (use: B,kB,kiB,MB,MiB,GB,GiB,TB,TiB)");
    ELLE_DAS_CLI_SYMBOL(clear_content, "remove all blocks from disk (filesystem storage only)");
    ELLE_DAS_CLI_SYMBOL(compatibility_version, "compatibility version to force");
    ELLE_DAS_CLI_SYMBOL(create, 'c', "create the {object}");
    ELLE_DAS_CLI_SYMBOL(create_home, "create user home directory of the form home/<user>");
    ELLE_DAS_CLI_SYMBOL(create_root, 'R', "create root directory");
    ELLE_DAS_CLI_SYMBOL(daemon, 'd', "run as a background daemon");
    ELLE_DAS_CLI_SYMBOL(default_permissions, 'd', "default permissions (optional: r,rw)");
    ELLE_DAS_CLI_SYMBOL(deny_storage, "deny user ability to contribute storage to the network");
    ELLE_DAS_CLI_SYMBOL(deny_write, "deny user write access to the network");
    ELLE_DAS_CLI_SYMBOL(description, "{object} description");
    ELLE_DAS_CLI_SYMBOL(disable_UTF_8_conversion, "disable FUSE conversion of UTF-8 to native format");
    ELLE_DAS_CLI_SYMBOL(disable_encrypt_at_rest, "disable at-rest encryption");
    ELLE_DAS_CLI_SYMBOL(disable_encrypt_rpc, "disable RPC encryption");
    ELLE_DAS_CLI_SYMBOL(disable_inherit, "make new files and directories not inherit permissions");
    ELLE_DAS_CLI_SYMBOL(disable_signature, "disable all block signature computation and validation");
    ELLE_DAS_CLI_SYMBOL(docker, "enable the Docker plugin");
    ELLE_DAS_CLI_SYMBOL(docker_descriptor_path, "path to add plugin descriptor");
    ELLE_DAS_CLI_SYMBOL(docker_home, "home directory to use for Docker user (default: /home/<docker-user>)");
    ELLE_DAS_CLI_SYMBOL(docker_mount_substitute, "[from:to|prefix] : Substitute 'from' to 'to' in advertised path");
    ELLE_DAS_CLI_SYMBOL(docker_socket_path, "path for plugin socket");
    ELLE_DAS_CLI_SYMBOL(docker_socket_port, "TCP port to use to communicate with Docker for random");
    ELLE_DAS_CLI_SYMBOL(docker_socket_tcp, "use a TCP socket for docker plugin");
    ELLE_DAS_CLI_SYMBOL(docker_user, "system user to use for docker plugin");
    ELLE_DAS_CLI_SYMBOL(domain, 'd', "LDAP domain");
    ELLE_DAS_CLI_SYMBOL(email, 'e', "user email");
    ELLE_DAS_CLI_SYMBOL(email_pattern, 'e', "email address pattern)");
    ELLE_DAS_CLI_SYMBOL(enable_inherit, 'i', "make new files and directories inherit permissions");
    ELLE_DAS_CLI_SYMBOL(encrypt,  "use encryption: no, lazy, yes (default: yes)");
    ELLE_DAS_CLI_SYMBOL(endpoint, "S3 endpoint");
    ELLE_DAS_CLI_SYMBOL(endpoints_file, "write node listening endpoints to file (format: host:port)");
    ELLE_DAS_CLI_SYMBOL(eviction_delay, 'e', "missing servers eviction delay (default: 10min)");
    ELLE_DAS_CLI_SYMBOL(fallback_xattrs, "use fallback special file if extended attributes are not supported");
    ELLE_DAS_CLI_SYMBOL(fetch, 'f', "fetch {object} from {hub}");
    ELLE_DAS_CLI_SYMBOL(fetch_endpoints, "fetch endpoints from {hub}");
    ELLE_DAS_CLI_SYMBOL(fetch_endpoints_interval, "period for repolling endpoints from the Hub in seconds");
    ELLE_DAS_CLI_SYMBOL(filter, 'f', "raw LDAP query to use (default: objectClass=posixGroup)");
    ELLE_DAS_CLI_SYMBOL(force, 'f', "do not ask for user confirmation");
    ELLE_DAS_CLI_SYMBOL(full, "include private key (do not use unless you understand the implications");
    ELLE_DAS_CLI_SYMBOL(fullname, "user full name");
    ELLE_DAS_CLI_SYMBOL(fullname_pattern, 'F', "fullname pattern)");
    ELLE_DAS_CLI_SYMBOL(fuse_option, "option to pass directly to FUSE");
    ELLE_DAS_CLI_SYMBOL(group, 'g', "group {action} {object} for");
    ELLE_DAS_CLI_SYMBOL(grpc, "start gRPC server on given endpoint");
    ELLE_DAS_CLI_SYMBOL(grpc_port_file, "write gRPC listening port to file");
    ELLE_DAS_CLI_SYMBOL(help, 'h', "show this help message");
    ELLE_DAS_CLI_SYMBOL(hold, "keep storage online until this process terminates"); // XXX[Storage]: Silo or Storage.
    ELLE_DAS_CLI_SYMBOL(home, 'h', "create a home directory for the invited user");
    ELLE_DAS_CLI_SYMBOL(host, "SSH host");
    ELLE_DAS_CLI_SYMBOL(ignore_non_linked, "do not consider problematic non-linked networks");
    ELLE_DAS_CLI_SYMBOL(input, 'i', "file to read {object} from");
    ELLE_DAS_CLI_SYMBOL(k, "number of groups (default: 1)");
    ELLE_DAS_CLI_SYMBOL(kalimero, "use a Kalimero overlay network. Used for local testing");
    ELLE_DAS_CLI_SYMBOL(kelips, "use a Kelips overlay network (default)");
    ELLE_DAS_CLI_SYMBOL(kelips_contact_timeout, "ping timeout before considering a peer lost");
    ELLE_DAS_CLI_SYMBOL(key, 'k', "RSA key pair in PEM format - e.g. your SSH key");
    ELLE_DAS_CLI_SYMBOL(kouncil, "use a Kouncil overlay network");
    ELLE_DAS_CLI_SYMBOL(ldap_name, 'l', "user LDAP distinguished name");
    ELLE_DAS_CLI_SYMBOL(listen, "specify which IP address to listen on (default: all)");
    ELLE_DAS_CLI_SYMBOL(log, "specify the destination for logs");
    ELLE_DAS_CLI_SYMBOL(log_level, "log level to start volumes with (default: LOG)");
    ELLE_DAS_CLI_SYMBOL(log_path, "store volume logs in given path");
    ELLE_DAS_CLI_SYMBOL(login_user, "login with selected user(s), of form 'user:password'");
    ELLE_DAS_CLI_SYMBOL(map_other_permissions, "allow chmod to set world permissions");
    ELLE_DAS_CLI_SYMBOL(match, 'm', "regular expression specifying names of the {objects} {action}");
    ELLE_DAS_CLI_SYMBOL(mode, 'm', "access mode {action}: r, w, rw, none");
    ELLE_DAS_CLI_SYMBOL(monitoring, "enable monitoring");
    ELLE_DAS_CLI_SYMBOL(mountpoint, 'm', "where to mount the filesystem");
    ELLE_DAS_CLI_SYMBOL(name, 'n', "name of the {object} {action}", true);
    ELLE_DAS_CLI_SYMBOL(network, 'N', "network {action} {object} for");
    ELLE_DAS_CLI_SYMBOL(number, "limit the number of {objects} {action}");
    ELLE_DAS_CLI_SYMBOL(no_avatar, "do not {action} avatars");
    ELLE_DAS_CLI_SYMBOL(no_color, "don't use colored output");
    ELLE_DAS_CLI_SYMBOL(no_consensus, "use no consensus algorithm");
    ELLE_DAS_CLI_SYMBOL(no_countdown, "do not show countdown timer");
    ELLE_DAS_CLI_SYMBOL(no_local_endpoints, "disable automatic detection of local endpoints");
    ELLE_DAS_CLI_SYMBOL(no_public_endpoints, "disable automatic detection of public endpoints");
    ELLE_DAS_CLI_SYMBOL(node_id, "node ID");
    ELLE_DAS_CLI_SYMBOL(nodes, "estimate of the total number of nodes");
    ELLE_DAS_CLI_SYMBOL(object_class, 'o', "filter results (default: posixGroup)");
    ELLE_DAS_CLI_SYMBOL(operation, 'O', "operation to {action}");
    ELLE_DAS_CLI_SYMBOL(others_mode, 'o', "access mode {action} for other users: r, w, rw, none");
    ELLE_DAS_CLI_SYMBOL(output, 'o', "file to write the {object} to");
    ELLE_DAS_CLI_SYMBOL(packet_size, 's', "size of the packet to send (client only)");
    ELLE_DAS_CLI_SYMBOL(packets_count, 'n', "number of packets to exchange (client only)");
    ELLE_DAS_CLI_SYMBOL(passphrase, "passphrase to secure identity (default: prompt for passphrase)");
    ELLE_DAS_CLI_SYMBOL(passport, "create passports for each invitee");
    ELLE_DAS_CLI_SYMBOL(password, 'P', "password to authenticate with {hub}");
    ELLE_DAS_CLI_SYMBOL(path, "file whose {object} {action}");
    ELLE_DAS_CLI_SYMBOL(paths, 'p', "paths to blocks");
    ELLE_DAS_CLI_SYMBOL(paxos, "use Paxos consensus algorithm (default)");
    ELLE_DAS_CLI_SYMBOL(paxos_rebalancing_auto_expand, "whether to automatically rebalance under-replicated blocks");
    ELLE_DAS_CLI_SYMBOL(paxos_rebalancing_inspect, "whether to inspect all blocks on startup and trigger rebalancing");
    ELLE_DAS_CLI_SYMBOL(peer, "peer address or file with list of peer addresses (host:port)");
    ELLE_DAS_CLI_SYMBOL(peers, "list connected peers");
    ELLE_DAS_CLI_SYMBOL(peers_file, "periodically write list of known peers to given file");
    ELLE_DAS_CLI_SYMBOL(permissions, "set default user permissions to XXX");
    ELLE_DAS_CLI_SYMBOL(port, "outbound port to use");
    ELLE_DAS_CLI_SYMBOL(port_file, "write node listening port to file");
    ELLE_DAS_CLI_SYMBOL(prometheus, "start Prometheus server on given endpoint");
    ELLE_DAS_CLI_SYMBOL(protocol, "RPC protocol to use: tcp, utp, all (default: all)");
    ELLE_DAS_CLI_SYMBOL(publish, "alias for --fetch-endpoints --push-endpoints");
    ELLE_DAS_CLI_SYMBOL(pull, "pull {object} from {hub}");
    ELLE_DAS_CLI_SYMBOL(purge, "purge objects owned by the {object}");
    ELLE_DAS_CLI_SYMBOL(push, 'p', "push {object} to {hub}");
    ELLE_DAS_CLI_SYMBOL(push_endpoints, "push endpoints to {hub}");
    ELLE_DAS_CLI_SYMBOL(push_key_value_store, "push the key-value store to {hub}");
    ELLE_DAS_CLI_SYMBOL(push_network, "push the network to {hub}");
    ELLE_DAS_CLI_SYMBOL(push_passport, "push passport to {hub}");
    ELLE_DAS_CLI_SYMBOL(push_user, "push user to {hub}");
    ELLE_DAS_CLI_SYMBOL(readonly, "mount as readonly");
    ELLE_DAS_CLI_SYMBOL(receive, "receive an object from another device using {hub}");
    ELLE_DAS_CLI_SYMBOL(recursive, 'R', "{verb} {object} recursively");
    ELLE_DAS_CLI_SYMBOL(redundancy, "describe data redundancy");
    ELLE_DAS_CLI_SYMBOL(region, "AWS region");
    ELLE_DAS_CLI_SYMBOL(register_service, 'r', "register volume in the network");
    ELLE_DAS_CLI_SYMBOL(remove, "remove users, administrators and groups from group (prefix: @<group>, ^<admin>");
    ELLE_DAS_CLI_SYMBOL(remove_admin, "remove administrator from group");
    ELLE_DAS_CLI_SYMBOL(remove_group, "remove group from group");
    ELLE_DAS_CLI_SYMBOL(remove_user, "remove user from group");
    ELLE_DAS_CLI_SYMBOL(replication_factor, 'r', "data replication factor (default: 1)");
    ELLE_DAS_CLI_SYMBOL(resign_on_shutdown, "rebalance blocks out when shutting down");
    ELLE_DAS_CLI_SYMBOL(restart, "restart {object}");
    ELLE_DAS_CLI_SYMBOL(script, 's', "suppress extraneous human friendly messages and use JSON output");
    ELLE_DAS_CLI_SYMBOL(searchbase, 'b', "search starting point (without domain)"); // FIXME: why not search_base?
    ELLE_DAS_CLI_SYMBOL(server, "connectivity server address (default = 192.241.139.66)");
    ELLE_DAS_CLI_SYMBOL(service, "fetch {object} from the network, not beyond");
    ELLE_DAS_CLI_SYMBOL(show, "list group users, administrators and description");
    ELLE_DAS_CLI_SYMBOL(silo, 'S', "silo to contribute (optional, data striped over multiple)");
    ELLE_DAS_CLI_SYMBOL(silo_class, "silo class to use: STANDARD, STANDARD_IA, REDUCED_REDUNDANCY (default: bucket default)");
    ELLE_DAS_CLI_SYMBOL(ssh, "store blocks via SSH");
    ELLE_DAS_CLI_SYMBOL(stat, "show the remaining asynchronous operations count and size");
    ELLE_DAS_CLI_SYMBOL(tcp_heartbeat, "tcp heartbeat period and timeout");
    ELLE_DAS_CLI_SYMBOL(tcp_port, 't', "port to perform tcp tests on");
    ELLE_DAS_CLI_SYMBOL(traverse, 't', "set read permission on parent directories");
    ELLE_DAS_CLI_SYMBOL(upnp_tcp_port, "port to try to get an tcp upnp connection on");
    ELLE_DAS_CLI_SYMBOL(upnp_udt_port, "port to try to get an udt upnp connection on");
    ELLE_DAS_CLI_SYMBOL(user, 'u', "user {action} {object} for");
    ELLE_DAS_CLI_SYMBOL(username_pattern, 'U', "hub unique username to set (default: $(cn)%). Remove the '%' to disable unique username generator");
    ELLE_DAS_CLI_SYMBOL(utp_port, 'u', "port to perform utp tests on (if unspecified, --xored-utp-port = utp-port + 1)");
    ELLE_DAS_CLI_SYMBOL(value, 'v', "value {action}");
    ELLE_DAS_CLI_SYMBOL(verbose, 'v', "use verbose output");
    ELLE_DAS_CLI_SYMBOL(xored, 'X', "performs test applying a 0xFF xor on the utp traffic, value=yes,no,both");
    ELLE_DAS_CLI_SYMBOL(xored_utp_port, 'x', "port to perform xored utp tests on");

    ELLE_DAS_CLI_SYMBOL_NAMED(delete, delete_, "delete the {object}");

    ELLE_DAS_SYMBOL(acl);
    ELLE_DAS_SYMBOL(block);
    ELLE_DAS_SYMBOL(call);
    ELLE_DAS_SYMBOL(configuration);
    ELLE_DAS_SYMBOL(connectivity);
    ELLE_DAS_SYMBOL(credentials);
    ELLE_DAS_SYMBOL(describe);
    ELLE_DAS_SYMBOL(deserialize);
    ELLE_DAS_SYMBOL(device);
    ELLE_DAS_SYMBOL(disable_storage);
    ELLE_DAS_SYMBOL(doctor);
    ELLE_DAS_SYMBOL(dropbox);
    ELLE_DAS_SYMBOL(enable_storage);
    ELLE_DAS_SYMBOL(filesystem);
    ELLE_DAS_SYMBOL(gcs);
    ELLE_DAS_SYMBOL(getxattr);
    ELLE_DAS_SYMBOL(google_drive);
    ELLE_DAS_SYMBOL(hash);
    ELLE_DAS_SYMBOL(import);
    ELLE_DAS_SYMBOL(inspect);
    ELLE_DAS_SYMBOL(invite);
    ELLE_DAS_SYMBOL(join);
    ELLE_DAS_SYMBOL(journal);
    ELLE_DAS_SYMBOL(kvs);
    ELLE_DAS_SYMBOL(ldap);
    ELLE_DAS_SYMBOL(link);
    ELLE_DAS_SYMBOL(list);
    ELLE_DAS_SYMBOL(list_services);
    ELLE_DAS_SYMBOL(list_silos);
    ELLE_DAS_SYMBOL(login);
    ELLE_DAS_SYMBOL(manage_volumes);
    ELLE_DAS_SYMBOL(networking);
    ELLE_DAS_SYMBOL(populate_hub);
    ELLE_DAS_SYMBOL(populate_network);
    ELLE_DAS_SYMBOL(run);
    ELLE_DAS_SYMBOL(s3);
    ELLE_DAS_SYMBOL(set);
    ELLE_DAS_SYMBOL(setxattr);
    ELLE_DAS_SYMBOL(signup);
    ELLE_DAS_SYMBOL(start);
    ELLE_DAS_SYMBOL(status);
    ELLE_DAS_SYMBOL(stop);
    ELLE_DAS_SYMBOL(syscall);
    ELLE_DAS_SYMBOL(system);
    ELLE_DAS_SYMBOL(transmit);
    ELLE_DAS_SYMBOL(unlink);
    ELLE_DAS_SYMBOL(update);
    ELLE_DAS_SYMBOL(version);

    ELLE_DAS_SYMBOL_NAMED(export, export_);
    ELLE_DAS_SYMBOL_NAMED(register, register_);

    // Modes must be defined twice: once as a regular symbol, then as
    // a `modes::mode_*` symbol.
    namespace modes
    {
      ELLE_DAS_SYMBOL(mode_add);
      ELLE_DAS_SYMBOL(mode_all);
      ELLE_DAS_SYMBOL(mode_configuration);
      ELLE_DAS_SYMBOL(mode_connectivity);
      ELLE_DAS_SYMBOL(mode_create);
      ELLE_DAS_SYMBOL(mode_delete);
      ELLE_DAS_SYMBOL(mode_describe);
      ELLE_DAS_SYMBOL(mode_deserialize);
      ELLE_DAS_SYMBOL(mode_disable_storage);
      ELLE_DAS_SYMBOL(mode_dropbox);
      ELLE_DAS_SYMBOL(mode_enable_storage);
      ELLE_DAS_SYMBOL(mode_export);
      ELLE_DAS_SYMBOL(mode_fetch);
      ELLE_DAS_SYMBOL(mode_filesystem);
      ELLE_DAS_SYMBOL(mode_gcs);
      ELLE_DAS_SYMBOL(mode_get_xattr);
      ELLE_DAS_SYMBOL(mode_google_drive);
      ELLE_DAS_SYMBOL(mode_group);
      ELLE_DAS_SYMBOL(mode_hash);
      ELLE_DAS_SYMBOL(mode_import);
      ELLE_DAS_SYMBOL(mode_inspect);
      ELLE_DAS_SYMBOL(mode_invite);
      ELLE_DAS_SYMBOL(mode_join);
      ELLE_DAS_SYMBOL(mode_ldap);
      ELLE_DAS_SYMBOL(mode_link);
      ELLE_DAS_SYMBOL(mode_list);
      ELLE_DAS_SYMBOL(mode_list_services);
      ELLE_DAS_SYMBOL(mode_list_silos);
      ELLE_DAS_SYMBOL(mode_log);
      ELLE_DAS_SYMBOL(mode_login);
      ELLE_DAS_SYMBOL(mode_manage_volumes);
      ELLE_DAS_SYMBOL(mode_mount);
      ELLE_DAS_SYMBOL(mode_networking);
      ELLE_DAS_SYMBOL(mode_populate_hub);
      ELLE_DAS_SYMBOL(mode_populate_network);
      ELLE_DAS_SYMBOL(mode_pull);
      ELLE_DAS_SYMBOL(mode_push);
      ELLE_DAS_SYMBOL(mode_receive);
      ELLE_DAS_SYMBOL(mode_register);
      ELLE_DAS_SYMBOL(mode_run);
      ELLE_DAS_SYMBOL(mode_s3);
      ELLE_DAS_SYMBOL(mode_set);
      ELLE_DAS_SYMBOL(mode_set_xattr);
      ELLE_DAS_SYMBOL(mode_signup);
      ELLE_DAS_SYMBOL(mode_start);
      ELLE_DAS_SYMBOL(mode_stat);
      ELLE_DAS_SYMBOL(mode_status);
      ELLE_DAS_SYMBOL(mode_stop);
      ELLE_DAS_SYMBOL(mode_system);
      ELLE_DAS_SYMBOL(mode_transmit);
      ELLE_DAS_SYMBOL(mode_unlink);
      ELLE_DAS_SYMBOL(mode_update);
    }
  }
}
