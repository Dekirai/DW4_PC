#include "dw4/viewer.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {
    ++checks;
    if(!value)throw std::runtime_error(reason);
}
template<class F> void rejects(F&& call,std::string_view expected) {
    ++checks;
    try {call();}
    catch(const ViewerError& error) {
        if(std::string_view(error.what()).find(expected)==std::string_view::npos)
            throw std::runtime_error("Viewer failure lost its reached null-owner distinction.");
        return;
    }
    throw std::runtime_error("A represented null viewer owner fabricated a view matrix.");
}
void check_zero_default(const AnimationMatrix& matrix) {
    for(std::size_t index=0;index<matrix.size();++index)
        check(std::bit_cast<std::uint32_t>(matrix[index])==0u,
            "Viewer matrix factory replaced the original zero default with an invented identity.");
}
void explicit_construction() {
    const auto manager=std::make_shared<ViewerSceneInfoManager>();
    check(!manager->parameters,"Constructing a viewer manager silently created parameters.");
    const auto first=make_viewer_parameters();
    const auto second=make_viewer_parameters();
    check(first && second && first!=second,"Explicit viewer factories reused one parameters allocation.");
    check_zero_default(first->projection_matrix);
    check_zero_default(first->view_matrix);
    check_zero_default(second->projection_matrix);
    check_zero_default(second->view_matrix);
    first->view_matrix[12]=31;
    check(second->view_matrix[12]==0 && first->projection_matrix[12]==0,
        "Independent viewer fields or factory allocations unexpectedly share matrix backing.");
    check(!manager->parameters,"Creating unrelated parameters implicitly attached them to a manager.");
}
void reached_null_owners() {
    rejects([] {capture_view_matrix({});},"manager");
    const auto manager=std::make_shared<ViewerSceneInfoManager>();
    rejects([&] {capture_view_matrix(manager);},"parameters");
    check(!manager->parameters,"A null-parameters failure silently prepared a replacement owner.");
    manager->parameters=make_viewer_parameters();
    const auto matrix=capture_view_matrix(manager);
    manager->parameters.reset();
    rejects([&] {capture_view_matrix(manager);},"parameters");
    check_zero_default(*matrix);
}
void inline_alias_and_live_updates() {
    const auto manager=std::make_shared<ViewerSceneInfoManager>();
    manager->parameters=make_viewer_parameters();
    const auto first=capture_view_matrix(manager);
    const auto second=capture_view_matrix(manager);
    check(first.get()==&manager->parameters->view_matrix && second.get()==first.get(),
        "Viewer capture copied the matrix instead of selecting its inline storage.");
    check(!first.owner_before(manager->parameters) && !manager->parameters.owner_before(first),
        "Captured viewer matrix does not retain the parameters control block.");
    manager->parameters->view_matrix[12]=83;
    check((*first)[12]==83 && (*second)[12]==83,"An already captured matrix did not observe live producer writes.");
    (*second)[13]=-19;
    check(manager->parameters->view_matrix[13]==-19,"Aliasing viewer writes did not reach the original inline matrix.");
    check_zero_default(manager->parameters->projection_matrix);

    // Capture is an ownership operation, not a matrix repair or validation pass.
    // Preserve raw payloads for the reached inverse's own arithmetic contract.
    manager->parameters->view_matrix[0]=std::bit_cast<float>(0x80000000u);
    manager->parameters->view_matrix[5]=std::bit_cast<float>(0x7fc01234u);
    const auto raw=capture_view_matrix(manager);
    check(std::bit_cast<std::uint32_t>((*raw)[0])==0x80000000u &&
          std::bit_cast<std::uint32_t>((*raw)[5])==0x7fc01234u,
        "Viewer capture sanitized the selected matrix instead of preserving its words.");
}
void replacement_and_lifetime() {
    auto manager=std::make_shared<ViewerSceneInfoManager>();
    manager->parameters=make_viewer_parameters();
    manager->parameters->view_matrix[14]=41;
    std::weak_ptr<ViewerParameters> original=manager->parameters;
    auto captured=capture_view_matrix(manager);
    const auto original_address=captured.get();

    manager->parameters=make_viewer_parameters();
    manager->parameters->view_matrix[14]=97;
    auto replacement=capture_view_matrix(manager);
    check(!original.expired() && captured.get()==original_address && (*captured)[14]==41,
        "Replacing viewer parameters retargeted or destroyed a captured matrix.");
    check(replacement.get()!=captured.get() && (*replacement)[14]==97,
        "A fresh viewer capture failed to select the current parameters allocation.");
    check(captured.owner_before(replacement) || replacement.owner_before(captured),
        "Replacement viewer parameters reused the old matrix's ownership identity.");

    std::weak_ptr<ViewerSceneInfoManager> weak_manager=manager;
    std::weak_ptr<ViewerParameters> weak_replacement=manager->parameters;
    manager.reset();
    check(weak_manager.expired(),"A captured matrix unnecessarily retained its former manager.");
    check(!original.expired() && !weak_replacement.expired() && (*captured)[14]==41 && (*replacement)[14]==97,
        "Destroying the manager invalidated a separately captured parameters matrix.");
    captured.reset();
    check(original.expired() && !weak_replacement.expired(),
        "The original parameters allocation outlived its final alias or released its replacement.");
    replacement.reset();
    check(weak_replacement.expired(),"A captured viewer matrix leaked its parameters allocation.");
}
void shared_parameters_identity() {
    const auto first=std::make_shared<ViewerSceneInfoManager>();
    const auto second=std::make_shared<ViewerSceneInfoManager>();
    first->parameters=make_viewer_parameters();
    second->parameters=first->parameters;
    const auto a=capture_view_matrix(first);
    const auto b=capture_view_matrix(second);
    check(a.get()==b.get() && !a.owner_before(b) && !b.owner_before(a),
        "Shared parameters owners became independent camera snapshots during capture.");
    (*a)[3]=7;
    check((*b)[3]==7,"Shared original parameters did not preserve live matrix aliasing.");
    first->parameters.reset();
    check(capture_view_matrix(second).get()==b.get(),"Clearing one manager replaced another manager's parameters.");
}
} // namespace
int main() {
    try {
        explicit_construction();reached_null_owners();inline_alias_and_live_updates();
        replacement_and_lifetime();shared_parameters_identity();
        std::cout<<"Passed "<<checks<<" native viewer ownership checks.\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"Viewer test failed: "<<error.what()<<'\n';
        return 1;
    }
}
