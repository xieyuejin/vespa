package com.yahoo.vespa.hosted.provision.provisioning;

import com.yahoo.vespa.hosted.provision.Node;

import java.util.Optional;

/**
 * Encapsulates all the information nessesary to prioritize node for allocation.
 *
 * @author smorgrav
 */
public class NodePriority {

    Node node;

    /** The free capacity excluding headroom, including retired allocations */
    ResourceCapacity freeParentCapacity = new ResourceCapacity();

    /** The parent host (docker or hypervisor) */
    Optional<Node> parent = Optional.empty();

    /** True if the node is allocated to a host that should be spare (without allocations */
    boolean violatesSpares;

    /** True if the node is allocated on slots that should be headroom */
    boolean violatesHeadroom;

    /** True if this is a node that has been retired earlier in the allocation process */
    boolean isSurplusNode;

    /** This node does not exist in the node repository yet */
    boolean isNewNode;

    /** True if exact flavor is specified by the allocation request and this node has this flavor */
    boolean preferredOnFlavor;

    /**
     * Compare two node priorities.
     *
     * Sorting order accending - thus negative if higher priority
     *
     * @return negative if first priority is higher than second node
     */
    static int compare(NodePriority n1, NodePriority n2) {
        // First always pick nodes without violation above nodes with violations
        if (!n1.violatesSpares && n2.violatesSpares) return -1;
        if (!n2.violatesSpares && n1.violatesSpares) return 1;
        if (!n1.violatesHeadroom && n2.violatesHeadroom) return -1;
        if (!n2.violatesHeadroom && n1.violatesHeadroom) return 1;

        // Choose active nodes
        if (n1.node.state().equals(Node.State.active) && !n2.node.state().equals(Node.State.active)) return -1;
        if (n2.node.state().equals(Node.State.active) && !n1.node.state().equals(Node.State.active)) return 1;

        // Choose active node that is not retired first (surplus is active but retired)
        if (!n1.isSurplusNode && n2.isSurplusNode) return -1;
        if (!n2.isSurplusNode && n1.isSurplusNode) return 1;

        // Choose inactive nodes
        if (n1.node.state().equals(Node.State.inactive) && !n2.node.state().equals(Node.State.inactive)) return -1;
        if (n2.node.state().equals(Node.State.inactive) && !n1.node.state().equals(Node.State.inactive)) return 1;

        // Choose reserved nodes
        if (n1.node.state().equals(Node.State.reserved) && !n2.node.state().equals(Node.State.reserved)) return -1;
        if (n2.node.state().equals(Node.State.reserved) && !n1.node.state().equals(Node.State.reserved)) return 1;

        // The node state has to be equal here
        if (!n1.node.state().equals(n2.node.state())) {
            throw new RuntimeException(
                    String.format("Nodes for allocation is not in expected state. Got %s and %s.",
                            n1.node.state(), n2.node.state()));
        }

        // Choose exact flavor
        if (n1.preferredOnFlavor && !n2.preferredOnFlavor) return -1;
        if (n2.preferredOnFlavor && !n1.preferredOnFlavor) return 1;

        // Choose docker node over non-docker node (this is to differentiate between docker replaces non-docker flavors)
        if (n1.parent.isPresent() && !n2.parent.isPresent()) return -1;
        if (n2.parent.isPresent() && !n1.parent.isPresent()) return 1;

        // Choose the node with parent node with the least capacity (TODO parameterize this as this is pretty much the core of the algorithm)
        int freeCapacity = n1.freeParentCapacity.compare(n2.freeParentCapacity);
        if (freeCapacity != 0) return freeCapacity;

        // Choose cheapest node
        if (n1.node.flavor().cost() < n2.node.flavor().cost()) return -1;
        if (n2.node.flavor().cost() < n1.node.flavor().cost()) return 1;


        // All else equal choose hostname lexically
        return n1.node.hostname().compareTo(n2.node.hostname());
    }
}
